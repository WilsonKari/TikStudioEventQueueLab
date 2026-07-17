#include "EventHost/TSChatExecutionHost.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>


static_assert(!std::is_copy_constructible_v<FTSChatExecutionHost>);
static_assert(!std::is_copy_assignable_v<FTSChatExecutionHost>);
static_assert(!std::is_move_constructible_v<FTSChatExecutionHost>);

namespace
{
    using TikStudio::Tests::Require;
{
    using namespace std::chrono_literals;

    struct FControlledClock
    {
        FTSEventQueueTimePoint Now{};

        [[nodiscard]]
        FTSNowProvider MakeProvider()
        {
            return [this]()
            {
                return Now;
            };
        }

        template <typename Rep, typename Period>
        void Advance(std::chrono::duration<Rep, Period> Delta)
        {
            Now += std::chrono::duration_cast<FTSEventQueueClock::duration>(
                Delta
            );
        }
    };

    [[nodiscard]]
    FTSChatInput MakeChatInput(const std::string& Label)
    {
        FTSChatInput Input;
        Input.Comment = Label;
        Input.Emotes = {
            FTSEmoteInfo{Label + "-emote-a", "https://example.test/a.png"},
            FTSEmoteInfo{Label + "-emote-b", "https://example.test/b.png"}
        };
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl = "https://example.test/user.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    void RequireChatInputEqual(
        const FTSChatInput& Actual,
        const FTSChatInput& Expected,
        const std::string& Context
    )
    {
        Require(Actual.Comment == Expected.Comment, Context + ": Comment");
        Require(
            Actual.Emotes.size() == Expected.Emotes.size(),
            Context + ": Emote count"
        );

        for (std::size_t Index = 0; Index < Expected.Emotes.size(); ++Index)
        {
            Require(
                Actual.Emotes[Index].EmoteId == Expected.Emotes[Index].EmoteId,
                Context + ": EmoteId"
            );
            Require(
                Actual.Emotes[Index].EmoteImageUrl ==
                    Expected.Emotes[Index].EmoteImageUrl,
                Context + ": EmoteImageUrl"
            );
        }

        Require(
            Actual.User.UniqueId == Expected.User.UniqueId,
            Context + ": UniqueId"
        );
        Require(
            Actual.User.Nickname == Expected.User.Nickname,
            Context + ": Nickname"
        );
        Require(
            Actual.User.ProfilePictureUrl ==
                Expected.User.ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(
            Actual.User.FollowRole == Expected.User.FollowRole,
            Context + ": FollowRole"
        );
        Require(
            Actual.User.bIsModerator == Expected.User.bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            Actual.User.bIsSubscriber == Expected.User.bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(
            Actual.User.bIsNewGifter == Expected.User.bIsNewGifter,
            Context + ": bIsNewGifter"
        );
        Require(
            Actual.User.TopGifterRank == Expected.User.TopGifterRank,
            Context + ": TopGifterRank"
        );
        Require(
            Actual.User.GifterLevel == Expected.User.GifterLevel,
            Context + ": GifterLevel"
        );
        Require(
            Actual.User.TeamMemberLevel == Expected.User.TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    [[nodiscard]]
    FTSEventQueueSettings MakeChatSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = 8s,
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Chat settings must exist");
        ChatSettings->bEnabled = true;
        ChatSettings->MaxSlots = MaxSlots;
        ChatSettings->TTL = TTL;
        ChatSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    FTSEmissionId RequireAcceptedAdmission(
        const FTSChatHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(
            Cycle.ProcessedCommand == ETSChatHostCommandKind::ChatInput,
            Context + ": command kind"
        );
        Require(
            Cycle.AdmissionResult.has_value() &&
                !Cycle.CompletionResult.has_value(),
            Context + ": admission result invariant"
        );
        Require(
            Cycle.AdmissionResult->Status ==
                ETSPipelineAdmissionStatus::Accepted &&
                Cycle.AdmissionResult->EnqueueResult.has_value(),
            Context + ": admission must be accepted"
        );

        const FTSEmissionId EmissionId =
            Cycle.AdmissionResult->EnqueueResult->AdmittedEmission.EmissionId;
        Require(EmissionId != 0, Context + ": valid EmissionId");
        return EmissionId;
    }

    [[nodiscard]]
    const FTSChatProcessingDispatch& RequireDispatch(
        const FTSChatHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        Require(
            Cycle.Dispatch->Emission.EmissionId != 0,
            Context + ": dispatch identity"
        );
        return *Cycle.Dispatch;
    }

    void TestEmptyHostCycle()
    {
        FTSChatExecutionHost Host;
        const FTSChatHostCycleResult Cycle = Host.RunOneCycle();

        Require(
            Cycle.ProcessedCommand == ETSChatHostCommandKind::None,
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
        FTSChatExecutionHost Host;
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
        const FTSChatHostCycleResult Cycle = Host.RunOneCycle();
        (void)RequireAcceptedAdmission(Cycle, "Worker PostChat");
        const FTSChatProcessingDispatch& Dispatch =
            RequireDispatch(Cycle, "Worker PostChat");
        RequireChatInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker PostChat owned payload"
        );
    }

    void TestSequentialFifoAndAsynchronousCompletion()
    {
        FTSChatExecutionHost Host;
        const FTSChatInput InputA = MakeChatInput("fifo-a");
        const FTSChatInput InputB = MakeChatInput("fifo-b");

        Require(Host.PostChat(InputA), "First FIFO publication must signal");
        Require(!Host.PostChat(InputB), "Second FIFO publication must not signal");

        const FTSChatHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedAdmission(CycleA, "FIFO cycle A");
        const FTSChatProcessingDispatch& DispatchA =
            RequireDispatch(CycleA, "FIFO cycle A");
        Require(
            DispatchA.Emission.EmissionId == AId,
            "FIFO cycle A dispatch identity"
        );
        RequireChatInputEqual(
            DispatchA.Payload.Input,
            InputA,
            "FIFO first payload"
        );
        Require(
            CycleA.bMoreCommandsPending,
            "FIFO cycle A must leave B published"
        );

        const FTSChatHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedAdmission(CycleB, "FIFO cycle B");
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
        const FTSChatHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.ProcessedCommand ==
                ETSChatHostCommandKind::ProcessingCompletion &&
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
            RequireDispatch(CompletionCycle, "FIFO completion cycle");
        Require(
            DispatchB.Emission.EmissionId == BId,
            "FIFO completion must dispatch B after A"
        );
        RequireChatInputEqual(
            DispatchB.Payload.Input,
            InputB,
            "FIFO second payload"
        );
    }

    void TestWorkerCompletionReleasesCapacity()
    {
        FTSChatExecutionHost Host(MakeChatSettings(1));
        Require(
            Host.PostChat(MakeChatInput("capacity-a")),
            "Capacity A publication must signal"
        );
        const FTSChatHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedAdmission(AdmissionCycle, "Capacity A");
        (void)RequireDispatch(AdmissionCycle, "Capacity A");

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
        const FTSChatHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.ProcessedCommand ==
                ETSChatHostCommandKind::ProcessingCompletion &&
                CompletionCycle.CompletionResult.has_value() &&
                CompletionCycle.CompletionResult->ConfirmResult.has_value(),
            "Worker completion must confirm A"
        );

        Require(
            Host.PostChat(MakeChatInput("capacity-b")),
            "Capacity B publication must signal"
        );
        const FTSChatHostCycleResult NextAdmission = Host.RunOneCycle();
        (void)RequireAcceptedAdmission(
            NextAdmission,
            "Capacity after worker completion"
        );
    }

    void TestCancelAdvancesWithExplicitPump()
    {
        FTSChatExecutionHost Host;
        Require(Host.PostChat(MakeChatInput("cancel-a")), "Cancel A signal");
        Require(!Host.PostChat(MakeChatInput("cancel-b")), "Cancel B signal");

        const FTSChatHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedAdmission(CycleA, "Cancel A admission");
        (void)RequireDispatch(CycleA, "Cancel A admission");

        const FTSChatHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedAdmission(CycleB, "Cancel B admission");
        Require(
            CycleB.PumpResult.has_value() &&
                CycleB.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Cancel B must remain Pending while A is Processing"
        );

        Require(
            Host.PostChatCompletion(AId, ETSProcessingResult::Cancelled),
            "Cancel completion must signal"
        );
        const FTSChatHostCycleResult CancelCycle = Host.RunOneCycle();
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
            RequireDispatch(CancelCycle, "Cancel cycle")
                    .Emission.EmissionId == BId,
            "Cancel cycle must dispatch the pumped B"
        );
    }

    void TestWrongCompletionPreservesLaterCommand()
    {
        FTSChatExecutionHost Host;
        Require(Host.PostChat(MakeChatInput("error-a")), "Error A signal");
        Require(!Host.PostChat(MakeChatInput("error-b")), "Error B signal");

        const FTSChatHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedAdmission(CycleA, "Error A admission");
        (void)RequireDispatch(CycleA, "Error A admission");

        const FTSChatHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedAdmission(CycleB, "Error B admission");
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
        const FTSChatHostCycleResult RecoveryCycle = Host.RunOneCycle();
        Require(
            RecoveryCycle.ProcessedCommand ==
                ETSChatHostCommandKind::ProcessingCompletion &&
                RecoveryCycle.CompletionResult.has_value() &&
                RecoveryCycle.CompletionResult->EmissionId == AId &&
                RecoveryCycle.CompletionResult->ConfirmResult.has_value(),
            "Correct A completion must remain queued after the failed command"
        );
        Require(
            RequireDispatch(RecoveryCycle, "Recovery cycle")
                    .Emission.EmissionId == BId,
            "B must dispatch after the surviving A completion"
        );
    }

    void TestPendingChatExpiresWhileAnotherIsProcessing()
    {
        FControlledClock Clock;
        FTSChatExecutionHost Host(
            MakeChatSettings(10, 5s),
            Clock.MakeProvider()
        );
        Require(Host.PostChat(MakeChatInput("ttl-a")), "TTL A signal");
        const FTSChatHostCycleResult CycleA = Host.RunOneCycle();
        const FTSEmissionId AId =
            RequireAcceptedAdmission(CycleA, "TTL A admission");
        (void)RequireDispatch(CycleA, "TTL A admission");

        Require(Host.PostChat(MakeChatInput("ttl-b")), "TTL B signal");
        const FTSChatHostCycleResult CycleB = Host.RunOneCycle();
        const FTSEmissionId BId =
            RequireAcceptedAdmission(CycleB, "TTL B admission");
        Require(
            CycleB.PumpResult.has_value() &&
                CycleB.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "TTL B must remain Pending behind A"
        );

        Clock.Advance(5s);
        const FTSChatHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand == ETSChatHostCommandKind::None,
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
        const FTSChatHostCycleResult CompletionCycle = Host.RunOneCycle();
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
        FTSChatExecutionHost Host(MakeChatSettings(), std::move(NowProvider));
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

        const FTSChatHostCycleResult OwnerCycle = Host.RunOneCycle();
        (void)RequireAcceptedAdmission(OwnerCycle, "Owner recovery cycle");
        (void)RequireDispatch(OwnerCycle, "Owner recovery cycle");
    }

    void TestExplicitPumpWorksWhenAutoPumpIsDisabled()
    {
        FTSChatExecutionHost Host(
            MakeChatSettings(10, 8s, false, true)
        );
        Require(
            Host.PostChat(MakeChatInput("explicit-pump")),
            "Explicit Pump publication must signal"
        );

        const FTSChatHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedAdmission(Cycle, "Explicit Pump admission");
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
            RequireDispatch(Cycle, "Explicit Pump admission")
                    .Emission.EmissionId == EmissionId,
            "Explicit Pump must dispatch in the same cycle"
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
    }
}
