#include "EventHost/TSEventExecutionHost.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<FTSEventExecutionHost>);
static_assert(!std::is_copy_assignable_v<FTSEventExecutionHost>);
static_assert(!std::is_move_constructible_v<FTSEventExecutionHost>);
static_assert(!std::is_move_assignable_v<FTSEventExecutionHost>);

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestEmptyHostCycle()
    {
        FTSEventExecutionHost Host;
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();

        Require(
            Cycle.ProcessedCommand == ETSEventHostCommandKind::None,
            "Empty Host must process no command"
        );
        Require(
            !Cycle.AdmissionResult.has_value() &&
                !Cycle.CompletionResult.has_value(),
            "Empty Host command result invariant"
        );
        Require(
            Cycle.DueExpirations.LifecycleEvents.empty(),
            "Empty Host must have no due expirations"
        );
        Require(
            Cycle.PumpResult.has_value() &&
                Cycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::QueueEmpty,
            "Empty Host must execute an empty Pump"
        );
        Require(!Cycle.Dispatch.has_value(), "Empty Host must not dispatch");
        Require(
            Cycle.NextWakeTime.Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Empty Host must have no wake scheduled"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "Empty Host must report no pending commands"
        );
    }

    void TestWorkerPostRunsOnOwner()
    {
        FTSEventExecutionHost Host;
        FTSChatInput Input = MakeChatInput("worker-post");
        const FTSChatInput Expected = Input;
        bool bScheduleRequested = false;
        std::exception_ptr WorkerError;

        std::thread Worker(
            [&]()
            {
                try
                {
                    bScheduleRequested = Host.PostChat(Input);
                }
                catch (...)
                {
                    WorkerError = std::current_exception();
                }
            }
        );
        Worker.join();

        if (WorkerError)
        {
            std::rethrow_exception(WorkerError);
        }

        Input.Comment = "mutated after publication";
        Input.Emotes.clear();
        Input.User.Nickname = "mutated worker input";

        Require(bScheduleRequested, "First worker PostChat must request scheduling");
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        (void)RequireAcceptedChatAdmission(Cycle, "Worker PostChat");
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(Cycle, "Worker PostChat");
        RequireChatPayloadMatchesInput(
            Dispatch.Payload,
            Expected,
            "Worker PostChat owned payload"
        );
    }

    void TestSequentialFifoAndAsynchronousCompletion()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput InputA = MakeChatInput("fifo-a");
        const FTSChatInput InputB = MakeChatInput("fifo-b");

        Require(Host.PostChat(InputA), "First FIFO publication must signal");
        Require(!Host.PostChat(InputB), "Second FIFO publication must not signal");

        const FTSEventHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedChatAdmission(CycleA, "FIFO cycle A");
        const FTSChatProcessingDispatch& DispatchA =
            RequireChatDispatch(CycleA, "FIFO cycle A");
        Require(
            DispatchA.Emission.EmissionId == AId,
            "FIFO cycle A dispatch identity"
        );
        RequireChatPayloadMatchesInput(
            DispatchA.Payload,
            InputA,
            "FIFO first payload"
        );
        Require(
            CycleA.bMoreCommandsPending,
            "FIFO cycle A must leave B published"
        );

        const FTSEventHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedChatAdmission(CycleB, "FIFO cycle B");
        Require(
            !CycleB.Dispatch.has_value(),
            "FIFO B must not dispatch while A is Processing"
        );
        Require(
            CycleB.PumpResult.has_value() &&
                CycleB.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "FIFO cycle B Pump must report Busy"
        );
        Require(
            !CycleB.bMoreCommandsPending,
            "FIFO cycle B must consume the second input command"
        );

        Require(
            Host.PostChatCompletion(AId, ETSProcessingResult::Succeeded),
            "FIFO completion publication must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.ProcessedCommand ==
                ETSEventHostCommandKind::ChatCompletion &&
                !CompletionCycle.AdmissionResult.has_value() &&
                CompletionCycle.CompletionResult.has_value(),
            "FIFO completion result invariant"
        );
        Require(
            CompletionCycle.CompletionResult->ProcessingResult ==
                ETSProcessingResult::Succeeded &&
                CompletionCycle.CompletionResult->ConfirmResult.has_value(),
            "FIFO A must complete successfully"
        );
        const FTSChatProcessingDispatch& DispatchB =
            RequireChatDispatch(CompletionCycle, "FIFO completion cycle");
        Require(
            DispatchB.Emission.EmissionId == BId,
            "FIFO completion must dispatch B after A"
        );
        RequireChatPayloadMatchesInput(
            DispatchB.Payload,
            InputB,
            "FIFO second payload"
        );
    }

    void TestWorkerCompletionReleasesCapacity()
    {
        FTSEventExecutionHost Host(MakeChatSettings(1));
        Require(
            Host.PostChat(MakeChatInput("capacity-a")),
            "Capacity A publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedChatAdmission(AdmissionCycle, "Capacity A");
        (void)RequireChatDispatch(AdmissionCycle, "Capacity A");

        bool bScheduleRequested = false;
        std::exception_ptr WorkerError;
        std::thread Worker(
            [&]()
            {
                try
                {
                    bScheduleRequested = Host.PostChatCompletion(
                        AId,
                        ETSProcessingResult::Succeeded
                    );
                }
                catch (...)
                {
                    WorkerError = std::current_exception();
                }
            }
        );
        Worker.join();

        if (WorkerError)
        {
            std::rethrow_exception(WorkerError);
        }

        Require(
            bScheduleRequested,
            "First worker completion must request scheduling"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.ProcessedCommand ==
                ETSEventHostCommandKind::ChatCompletion &&
                CompletionCycle.CompletionResult.has_value() &&
                CompletionCycle.CompletionResult->ConfirmResult.has_value(),
            "Worker completion must confirm A"
        );

        Require(
            Host.PostChat(MakeChatInput("capacity-b")),
            "Capacity B publication must signal"
        );
        const FTSEventHostCycleResult NextAdmission = Host.RunOneCycle();
        (void)RequireAcceptedChatAdmission(
            NextAdmission,
            "Capacity after worker completion"
        );
    }

    void TestCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host;
        Require(Host.PostChat(MakeChatInput("cancel-a")), "Cancel A signal");
        Require(!Host.PostChat(MakeChatInput("cancel-b")), "Cancel B signal");

        const FTSEventHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedChatAdmission(CycleA, "Cancel A admission");
        (void)RequireChatDispatch(CycleA, "Cancel A admission");

        const FTSEventHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedChatAdmission(CycleB, "Cancel B admission");
        Require(
            CycleB.PumpResult.has_value() &&
                CycleB.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Cancel B must remain Pending while A is Processing"
        );

        Require(
            Host.PostChatCompletion(AId, ETSProcessingResult::Cancelled),
            "Cancel completion must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        Require(
            CancelCycle.CompletionResult.has_value() &&
                CancelCycle.CompletionResult->ProcessingResult ==
                    ETSProcessingResult::Cancelled &&
                CancelCycle.CompletionResult->CancelResult.has_value(),
            "Cancel cycle must expose CancelResult"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId == BId,
            "Cancel cycle must Pump B explicitly"
        );
        Require(
            RequireChatDispatch(CancelCycle, "Cancel cycle")
                    .Emission.EmissionId == BId,
            "Cancel cycle must dispatch the pumped B"
        );
    }

    void TestWrongCompletionPreservesLaterCommand()
    {
        FTSEventExecutionHost Host;
        Require(Host.PostChat(MakeChatInput("error-a")), "Error A signal");
        Require(!Host.PostChat(MakeChatInput("error-b")), "Error B signal");

        const FTSEventHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedChatAdmission(CycleA, "Error A admission");
        (void)RequireChatDispatch(CycleA, "Error A admission");

        const FTSEventHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedChatAdmission(CycleB, "Error B admission");
        Require(!CycleB.Dispatch.has_value(), "Error B must remain Bound");

        Require(
            Host.PostChatCompletion(BId, ETSProcessingResult::Succeeded),
            "Wrong completion must signal when the tray is empty"
        );
        Require(
            !Host.PostChatCompletion(AId, ETSProcessingResult::Succeeded),
            "Correct completion behind wrong command must not signal again"
        );

        bool bThrewLogicError = false;
        try
        {
            (void)Host.RunOneCycle();
        }
        catch (const std::logic_error&)
        {
            bThrewLogicError = true;
        }
        Require(
            bThrewLogicError,
            "Completing Bound B must propagate logic_error"
        );

        // No se republica A: el siguiente ciclo demuestra que sobrevivió detrás de B.
        const FTSEventHostCycleResult RecoveryCycle = Host.RunOneCycle();
        Require(
            RecoveryCycle.ProcessedCommand ==
                ETSEventHostCommandKind::ChatCompletion &&
                RecoveryCycle.CompletionResult.has_value() &&
                RecoveryCycle.CompletionResult->EmissionId == AId &&
                RecoveryCycle.CompletionResult->ConfirmResult.has_value(),
            "Correct A completion must remain queued after the failed command"
        );
        Require(
            RequireChatDispatch(RecoveryCycle, "Recovery cycle")
                    .Emission.EmissionId == BId,
            "B must dispatch after the surviving A completion"
        );
    }

    void TestPendingChatExpiresWhileAnotherIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatSettings(10, 5s),
            Clock.MakeProvider()
        );
        Require(Host.PostChat(MakeChatInput("ttl-a")), "TTL A signal");
        const FTSEventHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedChatAdmission(CycleA, "TTL A admission");
        (void)RequireChatDispatch(CycleA, "TTL A admission");

        Require(Host.PostChat(MakeChatInput("ttl-b")), "TTL B signal");
        const FTSEventHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedChatAdmission(CycleB, "TTL B admission");
        Require(
            CycleB.PumpResult.has_value() &&
                CycleB.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "TTL B must remain Pending behind A"
        );

        Clock.Advance(5s);
        const FTSEventHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand == ETSEventHostCommandKind::None,
            "Expiration maintenance must process no command"
        );
        Require(
            ExpirationCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.EmissionId == BId &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Expiration maintenance must discard B at its TTL"
        );
        Require(
            ExpirationCycle.PumpResult.has_value() &&
                ExpirationCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy,
            "Expiration maintenance Pump must remain Busy on A"
        );
        Require(
            !ExpirationCycle.Dispatch.has_value(),
            "Expiration maintenance must not dispatch another Chat"
        );

        Require(
            Host.PostChatCompletion(AId, ETSProcessingResult::Succeeded),
            "TTL A completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.CompletionResult.has_value() &&
                CompletionCycle.CompletionResult->ConfirmResult.has_value(),
            "A must remain completable after B expires"
        );
    }

    void TestRunOneCycleRejectsWrongThreadBeforeWork()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(MakeChatSettings(), std::move(NowProvider));
        Require(
            Host.PostChat(MakeChatInput("wrong-thread")),
            "Wrong-thread setup publication must signal"
        );

        bool bThrewLogicError = false;
        std::exception_ptr UnexpectedError;
        std::thread Worker(
            [&]()
            {
                try
                {
                    (void)Host.RunOneCycle();
                }
                catch (const std::logic_error&)
                {
                    bThrewLogicError = true;
                }
                catch (...)
                {
                    UnexpectedError = std::current_exception();
                }
            }
        );
        Worker.join();

        if (UnexpectedError)
        {
            std::rethrow_exception(UnexpectedError);
        }

        Require(
            bThrewLogicError,
            "Worker RunOneCycle must throw logic_error"
        );
        Require(
            NowCallCount.load() == 0,
            "Wrong-thread cycle must not capture core time"
        );

        const FTSEventHostCycleResult OwnerCycle = Host.RunOneCycle();
        (void)RequireAcceptedChatAdmission(OwnerCycle, "Owner recovery cycle");
        (void)RequireChatDispatch(OwnerCycle, "Owner recovery cycle");
    }

    void TestExplicitPumpWorksWhenAutoPumpIsDisabled()
    {
        FTSEventExecutionHost Host(
            MakeChatSettings(10, 8s, false, true)
        );
        Require(
            Host.PostChat(MakeChatInput("explicit-pump")),
            "Explicit Pump publication must signal"
        );

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedChatAdmission(Cycle, "Explicit Pump admission");
        Require(
            Cycle.AdmissionResult->EnqueueResult->AutoPumpOutcome.Status ==
                ETSPumpStatus::NotRequested,
            "Disabled Auto Pump must remain visible in admission"
        );
        Require(
            Cycle.PumpResult.has_value() &&
                Cycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Cycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "Host must explicitly Pump admitted work"
        );
        Require(
            RequireChatDispatch(Cycle, "Explicit Pump admission")
                    .Emission.EmissionId == EmissionId,
            "Explicit Pump must dispatch in the same cycle"
        );
    }

    void TestThrowingMaintenancePreservesPendingCommand()
    {
        std::size_t NowCallCount = 0;
        FTSNowProvider NowProvider = [&]() -> FTSEventQueueTimePoint
        {
            ++NowCallCount;
            if (NowCallCount == 1)
            {
                throw std::runtime_error("Controlled maintenance failure");
            }

            return FTSEventQueueTimePoint{};
        };

        FTSEventExecutionHost Host({}, std::move(NowProvider));
        const FTSChatInput FirstInput = MakeChatInput("retained-first");
        const FTSChatInput SecondInput = MakeChatInput("retained-second");
        Require(Host.PostChat(FirstInput), "Retained first input must signal");

        bool bThrew = false;
        try
        {
            (void)Host.RunOneCycle();
        }
        catch (const std::runtime_error&)
        {
            bThrew = true;
        }
        Require(
            bThrew && NowCallCount == 1,
            "Controlled maintenance failure must propagate"
        );
        Require(
            !Host.PostChat(SecondInput),
            "Failed maintenance must leave the first command in the FIFO"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId = RequireAcceptedChatAdmission(
            FirstCycle,
            "Retained first admission"
        );
        RequireChatPayloadMatchesInput(
            RequireChatDispatch(FirstCycle, "Retained first dispatch")
                .Payload,
            FirstInput,
            "Retained first payload"
        );
        Require(
            FirstCycle.bMoreCommandsPending,
            "Second command must remain queued behind the retained first command"
        );

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId = RequireAcceptedChatAdmission(
            SecondCycle,
            "Retained second admission"
        );
        Require(
            !SecondCycle.Dispatch.has_value() &&
                SecondCycle.PumpResult.has_value() &&
                SecondCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Retained FIFO must admit the second command behind the first"
        );

        Require(
            Host.PostChatCompletion(
                FirstId,
                ETSProcessingResult::Succeeded
            ),
            "Retained first completion must signal"
        );
        const FTSEventHostCycleResult FirstCompletionCycle =
            Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                FirstCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                FirstId,
                ETSProcessingResult::Succeeded,
                "Retained first completion"
            ),
            FirstId,
            "Retained first completion"
        );
        Require(
            RequireChatDispatch(
                FirstCompletionCycle,
                "Retained second dispatch"
            ).Emission.EmissionId == SecondId,
            "Retained second command must dispatch after the first completes"
        );

        Require(
            Host.PostChatCompletion(
                SecondId,
                ETSProcessingResult::Succeeded
            ),
            "Retained second completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::ChatCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Retained second cleanup"
            ),
            SecondId,
            "Retained second cleanup"
        );
    }

    void TestChatFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeChatInput("failed-chat"),
            MakeChatInput("after-failed-chat"),
            ETSEventHostCommandKind::ChatCompletion,
            [](FTSEventExecutionHost& Host, FTSChatInput Input)
            {
                return Host.PostChat(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostChatCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedChatAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireChatDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "Chat Failed"
        );
    }

    void TestHostUsesCustomChatCommandSettings()
    {
        FTSEventPipelineSettings InvalidSettings;
        InvalidSettings.Chat.CommandPrefix.clear();
        bool bInvalidThrew = false;
        try
        {
            FTSEventExecutionHost InvalidHost({}, {}, InvalidSettings);
            (void)InvalidHost;
        }
        catch (const std::invalid_argument&)
        {
            bInvalidThrew = true;
        }
        Require(bInvalidThrew, "Host must reject invalid Chat settings");

        FTSEventPipelineSettings PipelineSettings;
        PipelineSettings.Chat.bOnlyAllowCommands = true;
        PipelineSettings.Chat.CommandPrefix = "/t";
        PipelineSettings.Chat.bRequireCommandBoundary = true;
        FTSEventExecutionHost Host({}, {}, PipelineSettings);

        Require(Host.PostChat(MakeChatInput("normal")), "Normal post signal");
        const FTSEventHostCycleResult Filtered = Host.RunOneCycle();
        Require(
            Filtered.AdmissionResult.has_value() &&
                Filtered.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::NoEmission &&
                !Filtered.Dispatch.has_value(),
            "Host must consume a filtered normal Chat command"
        );

        FTSChatInput Command = MakeChatInput("command");
        Command.Comment = "/t jump";
        Require(Host.PostChat(Command), "Command post signal");
        const FTSEventHostCycleResult Accepted = Host.RunOneCycle();
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(Accepted, "Custom command dispatch");
        Require(
            Dispatch.Payload.Messages.size() == 1 &&
                Dispatch.Payload.Messages[0].bIsCommand,
            "Custom Host command must be classified in the payload"
        );
    }

    void TestHostAccumulatesPendingChatAndDispatchesItAfterCompletion()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput Active = MakeChatInput("active");
        Require(Host.PostChat(Active), "Active Chat signal");
        const FTSEventHostCycleResult ActiveCycle = Host.RunOneCycle();
        const FTSEmissionId ActiveId =
            RequireAcceptedChatAdmission(ActiveCycle, "Active Chat");
        (void)RequireChatDispatch(ActiveCycle, "Active Chat");

        FTSChatInput First = MakeChatInput("pending-first");
        FTSChatInput Second = MakeChatInput("pending-second");
        Second.User = First.User;
        Require(Host.PostChat(First), "Pending first signal");
        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId PendingId =
            RequireAcceptedChatAdmission(FirstCycle, "Pending first");
        Require(!FirstCycle.Dispatch.has_value(), "Pending first remains behind active");

        Require(Host.PostChat(Second), "Pending second signal");
        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        Require(
            SecondCycle.AdmissionResult.has_value() &&
                SecondCycle.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::Accumulated &&
                SecondCycle.AdmissionResult->AffectedEmissionId == PendingId &&
                !SecondCycle.Dispatch.has_value(),
            "Same-user pending message must accumulate through Host"
        );

        Require(
            Host.PostChatCompletion(ActiveId, ETSProcessingResult::Succeeded),
            "Active completion signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSChatProcessingDispatch& PendingDispatch =
            RequireChatDispatch(CompletionCycle, "Accumulated successor dispatch");
        Require(
            PendingDispatch.Emission.EmissionId == PendingId &&
                PendingDispatch.Payload.Messages.size() == 2 &&
                PendingDispatch.Payload.Messages[0].Comment == First.Comment &&
                PendingDispatch.Payload.Messages[1].Comment == Second.Comment,
            "Completion must dispatch the ordered accumulated batch"
        );
    }

    void TestHostSemanticLimitConsumesCommandWithoutBlockingFifo()
    {
        FTSEventPipelineSettings PipelineSettings;
        PipelineSettings.Chat.MaxMessageUtf8Bytes = 4;
        FTSEventExecutionHost Host({}, {}, PipelineSettings);

        FTSChatInput TooLarge = MakeChatInput("too-large");
        TooLarge.Comment = "12345";
        FTSChatInput Next = MakeChatInput("next");
        Next.Comment = "ok";
        Require(Host.PostChat(TooLarge), "Oversized signal");
        Require(!Host.PostChat(Next), "Next FIFO signal");

        const FTSEventHostCycleResult Rejected = Host.RunOneCycle();
        Require(
            Rejected.AdmissionResult.has_value() &&
                Rejected.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::RejectedSemanticLimit &&
                Rejected.bMoreCommandsPending,
            "Semantic rejection must consume only its own FIFO command"
        );
        const FTSEventHostCycleResult NextCycle = Host.RunOneCycle();
        (void)RequireAcceptedChatAdmission(NextCycle, "FIFO after semantic rejection");
        (void)RequireChatDispatch(NextCycle, "FIFO after semantic rejection");
    }

    void TestHostChatTimestampUsesOwnerControlledClock()
    {
        FControlledClock Clock;
        Clock.Advance(7s);
        FTSEventExecutionHost Host({}, Clock.MakeProvider());
        const FTSChatInput Input = MakeChatInput("timestamp");
        Require(Host.PostChat(Input), "Timestamp Chat signal");
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(Cycle, "Timestamp Chat dispatch");
        Require(
            Dispatch.Payload.Messages.size() == 1 &&
                Dispatch.Payload.Messages[0].ReceivedAt == Clock.Now,
            "Chat timestamp must come from the owner-thread controlled clock"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterChatHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Empty Host cycle", &TestEmptyHostCycle});
        Tests.push_back({"Worker PostChat runs on owner", &TestWorkerPostRunsOnOwner});
        Tests.push_back({"Sequential FIFO and asynchronous completion", &TestSequentialFifoAndAsynchronousCompletion});
        Tests.push_back({"Worker completion releases capacity", &TestWorkerCompletionReleasesCapacity});
        Tests.push_back({"Cancel advances with explicit Pump", &TestCancelAdvancesWithExplicitPump});
        Tests.push_back({"Wrong completion preserves later command", &TestWrongCompletionPreservesLaterCommand});
        Tests.push_back({"Pending Chat expires while another is Processing", &TestPendingChatExpiresWhileAnotherIsProcessing});
        Tests.push_back({"RunOneCycle rejects wrong thread before work", &TestRunOneCycleRejectsWrongThreadBeforeWork});
        Tests.push_back({"Explicit Pump works when Auto Pump is disabled", &TestExplicitPumpWorksWhenAutoPumpIsDisabled});
        Tests.push_back({"Throwing maintenance preserves the pending command", &TestThrowingMaintenancePreservesPendingCommand});
        Tests.push_back({"Chat Failed completion is terminal and Host recovers", &TestChatFailedCompletionIsTerminalAndHostRecovers});
        Tests.push_back({"Host uses custom Chat command settings", &TestHostUsesCustomChatCommandSettings});
        Tests.push_back({"Host accumulates pending Chat and dispatches after completion", &TestHostAccumulatesPendingChatAndDispatchesItAfterCompletion});
        Tests.push_back({"Host semantic limit consumes command without blocking FIFO", &TestHostSemanticLimitConsumesCommandWithoutBlockingFifo});
        Tests.push_back({"Host Chat timestamp uses owner controlled clock", &TestHostChatTimestampUsesOwnerControlledClock});
    }
}
