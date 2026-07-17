#include "EventHost/TSEventExecutionHost.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>
#include <variant>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestFollowInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSFollowInput Input = MakeFollowInput("follow-auto");
        const FTSFollowInput Expected = Input;

        Require(Host.PostFollow(Input), "First Follow publication must signal");
        Input.User.UniqueId = "mutated-after-post";
        Input.User.Nickname.clear();

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedFollowAdmission(Cycle, "Follow Auto Pump");
        const FTSFollowProcessingDispatch& Dispatch =
            RequireFollowDispatch(Cycle, "Follow Auto Pump");

        Require(
            Dispatch.Emission.EmissionId == EmissionId,
            "Follow Auto Pump dispatch identity"
        );
        RequireFollowInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Follow Auto Pump owned payload"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) == nullptr,
            "Follow Auto Pump must not expose a Chat dispatch"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "Follow Auto Pump must consume its only command"
        );

        Require(
            Host.PostFollowCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Follow Auto Pump completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::FollowCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Follow Auto Pump completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Follow Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value(),
            "Completed sole Follow must not produce another dispatch"
        );
    }

    void TestWorkerPostFollowRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(
            MakeFollowSettings(),
            std::move(NowProvider)
        );
        FTSFollowInput Input = MakeFollowInput("follow-worker");
        const FTSFollowInput Expected = Input;
        const std::thread::id OwnerThreadId = std::this_thread::get_id();
        std::thread::id WorkerThreadId;
        bool bScheduleRequested = false;
        std::exception_ptr WorkerError;

        std::thread Worker(
            [&]()
            {
                WorkerThreadId = std::this_thread::get_id();
                try
                {
                    bScheduleRequested = Host.PostFollow(Input);
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
            WorkerThreadId != OwnerThreadId,
            "Worker Follow publication must execute on a worker"
        );
        Require(
            bScheduleRequested && NowCallCount.load() == 0,
            "Worker PostFollow must only publish and request owner scheduling"
        );

        Input.User.Nickname = "mutated-worker-input";
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedFollowAdmission(Cycle, "Worker PostFollow");
        const FTSFollowProcessingDispatch& Dispatch =
            RequireFollowDispatch(Cycle, "Worker PostFollow");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the Follow pipeline"
        );
        RequireFollowInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker Follow owned payload"
        );

        Require(
            Host.PostFollowCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker Follow cleanup must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::FollowCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Worker Follow cleanup"
            ),
            EmissionId,
            "Worker Follow cleanup"
        );
    }

    void TestMixedChatAndFollowCommandsPreserveFifo()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-fifo-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-fifo-follow");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must remain queued");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Mixed FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Mixed FIFO Chat")
                    .Emission.EmissionId == ChatId,
            "Mixed FIFO must dispatch Chat first"
        );
        Require(
            ChatCycle.bMoreCommandsPending,
            "Mixed FIFO Chat cycle must leave Follow queued"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Mixed FIFO Follow");
        Require(
            !FollowCycle.Dispatch.has_value() &&
                FollowCycle.PumpResult.has_value() &&
                FollowCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Mixed FIFO Follow must remain Bound behind Chat"
        );
        Require(
            !FollowCycle.bMoreCommandsPending,
            "Mixed FIFO must consume Follow second"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Mixed FIFO Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Mixed FIFO Chat completion"
            ),
            ChatId,
            "Mixed FIFO Chat completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(
                ChatCompletionCycle,
                "Mixed FIFO Follow dispatch"
            );
        Require(
            FollowDispatch.Emission.EmissionId == FollowId,
            "Mixed FIFO completion must dispatch the bound Follow"
        );
        RequireFollowInputEqual(
            FollowDispatch.Payload.Input,
            FollowInput,
            "Mixed FIFO Follow payload"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Mixed FIFO Follow completion must signal"
        );
        const FTSEventHostCycleResult FollowCompletionCycle =
            Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                FollowCompletionCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Mixed FIFO Follow completion"
            ),
            FollowId,
            "Mixed FIFO Follow completion"
        );
    }

    void TestFollowCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;

        Require(
            Host.PostFollow(MakeFollowInput("follow-before-chat")),
            "Follow-before-Chat publication must signal"
        );
        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Follow before Chat");
        (void)RequireFollowDispatch(FollowCycle, "Follow before Chat");

        Require(
            Host.PostChat(MakeChatInput("chat-after-follow")),
            "Chat-after-Follow publication must signal"
        );
        const FTSEventHostCycleResult ChatAdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(
                ChatAdmissionCycle,
                "Chat after Follow"
            );
        Require(
            !ChatAdmissionCycle.Dispatch.has_value(),
            "Chat must remain Pending while Follow is Processing"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Follow completion before Chat must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Follow completion captures Chat"
            ),
            FollowId,
            "Follow completion captures Chat"
        );
        Require(
            !CompletionCycle.PumpResult.has_value(),
            "Confirm Auto Pump ready must be consumed before explicit Pump"
        );
        Require(
            RequireChatDispatch(
                CompletionCycle,
                "Follow completion captures Chat"
            ).Emission.EmissionId == ChatId,
            "Follow completion must dispatch ready Chat in the same cycle"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Captured Chat cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Captured Chat cleanup"
            ),
            ChatId,
            "Captured Chat cleanup"
        );
    }

    void TestChatCompletionCapturesReadyFollow()
    {
        FTSEventExecutionHost Host;

        Require(
            Host.PostChat(MakeChatInput("chat-before-follow")),
            "Chat-before-Follow publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Chat before Follow");
        (void)RequireChatDispatch(ChatCycle, "Chat before Follow");

        Require(
            Host.PostFollow(MakeFollowInput("follow-after-chat")),
            "Follow-after-Chat publication must signal"
        );
        const FTSEventHostCycleResult FollowAdmissionCycle =
            Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(
                FollowAdmissionCycle,
                "Follow after Chat"
            );
        Require(
            !FollowAdmissionCycle.Dispatch.has_value(),
            "Follow must remain Pending while Chat is Processing"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Chat completion before Follow must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Chat completion captures Follow"
            ),
            ChatId,
            "Chat completion captures Follow"
        );
        Require(
            !CompletionCycle.PumpResult.has_value(),
            "Confirm Auto Pump Follow must precede explicit Pump"
        );
        Require(
            RequireFollowDispatch(
                CompletionCycle,
                "Chat completion captures Follow"
            ).Emission.EmissionId == FollowId,
            "Chat completion must dispatch ready Follow in the same cycle"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Captured Follow cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Captured Follow cleanup"
            ),
            FollowId,
            "Captured Follow cleanup"
        );
    }

    void TestFollowCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host;

        Require(
            Host.PostFollow(MakeFollowInput("follow-cancel-first")),
            "First cancel Follow must signal"
        );
        Require(
            !Host.PostFollow(MakeFollowInput("follow-cancel-second")),
            "Second cancel Follow must remain queued"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedFollowAdmission(FirstCycle, "First cancel Follow");
        (void)RequireFollowDispatch(FirstCycle, "First cancel Follow");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedFollowAdmission(SecondCycle, "Second cancel Follow");
        Require(
            !SecondCycle.Dispatch.has_value(),
            "Second cancel Follow must remain Pending"
        );

        Require(
            Host.PostFollowCompletion(
                FirstId,
                ETSProcessingResult::Cancelled
            ),
            "Follow cancel completion must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "Follow cancel completion"
            ),
            FirstId,
            "Follow cancel completion"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "Follow cancel must explicitly Pump the second Follow"
        );
        Require(
            RequireFollowDispatch(CancelCycle, "Follow cancel next dispatch")
                    .Emission.EmissionId == SecondId,
            "Follow cancel must dispatch the second Follow in the same cycle"
        );

        Require(
            Host.PostFollowCompletion(
                SecondId,
                ETSProcessingResult::Succeeded
            ),
            "Second cancel Follow cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::FollowCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Second cancel Follow cleanup"
            ),
            SecondId,
            "Second cancel Follow cleanup"
        );
    }

    void TestWrongFamilyCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(MakeChatInput("wrong-family-chat")),
            "Wrong-family Chat publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(
                AdmissionCycle,
                "Wrong-family Chat admission"
            );
        (void)RequireChatDispatch(
            AdmissionCycle,
            "Wrong-family Chat admission"
        );

        Require(
            Host.PostFollowCompletion(
                ChatId,
                ETSProcessingResult::Succeeded
            ),
            "Wrong-family completion must publish"
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
            "Wrong-family Follow completion must fail in the owner cycle"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Correct Chat completion must still publish"
        );
        const FTSEventHostCycleResult RecoveryCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Recovery = RequireCompletion(
            RecoveryCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            ETSProcessingResult::Succeeded,
            "Wrong-family recovery"
        );
        RequireConfirmedCompletion(
            Recovery,
            ChatId,
            "Wrong-family recovery"
        );
        Require(
            Recovery.ConfirmResult->LifecycleEvents.size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong-family attempt must not create a Follow terminal or dispatch"
        );
    }

    void TestPendingFollowExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatFollowSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "Expiry Chat");

        Require(
            Host.PostFollow(MakeFollowInput("expiring-follow")),
            "Expiring Follow publication must signal"
        );
        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringFollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Expiring Follow");
        Require(
            !FollowCycle.Dispatch.has_value() &&
                FollowCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending Follow must schedule its expiration while Chat is Processing"
        );

        Clock.Advance(5s);
        const FTSEventHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand ==
                ETSEventHostCommandKind::None,
            "Expiration cycle must process no command"
        );
        Require(
            ExpirationCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.EmissionId == ExpiringFollowId &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Follow must expire at its TTL"
        );
        Require(
            ExpirationCycle.PumpResult.has_value() &&
                ExpirationCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !ExpirationCycle.Dispatch.has_value(),
            "Chat must remain Processing without a Follow dispatch"
        );
        Require(
            ExpirationCycle.NextWakeTime.Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Expired Follow must remove the scheduled wake"
        );

        Require(
            Host.PostFollow(MakeFollowInput("follow-after-expiry")),
            "Follow after expiration must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId =
            RequireAcceptedFollowAdmission(
                ReplacementCycle,
                "Follow after expiration"
            );
        Require(
            !ReplacementCycle.Dispatch.has_value(),
            "Replacement Follow must wait behind Processing Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Expiry Chat completion"
            ),
            ChatId,
            "Expiry Chat completion"
        );
        Require(
            RequireFollowDispatch(
                ChatCompletionCycle,
                "Replacement Follow dispatch"
            ).Emission.EmissionId == ReplacementId,
            "Replacement Follow must dispatch after Chat completes"
        );

        Require(
            Host.PostFollowCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement Follow completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::FollowCompletion,
                ReplacementId,
                ETSProcessingResult::Succeeded,
                "Replacement Follow cleanup"
            ),
            ReplacementId,
            "Replacement Follow cleanup"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterFollowHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Follow input Auto Pumps and dispatches", &TestFollowInputAutoPumpsAndDispatches});
        Tests.push_back({"Worker PostFollow runs on owner", &TestWorkerPostFollowRunsOnOwner});
        Tests.push_back({"Mixed Chat and Follow commands preserve FIFO", &TestMixedChatAndFollowCommandsPreserveFifo});
        Tests.push_back({"Follow completion captures ready Chat", &TestFollowCompletionCapturesReadyChat});
        Tests.push_back({"Chat completion captures ready Follow", &TestChatCompletionCapturesReadyFollow});
        Tests.push_back({"Follow cancel advances with explicit Pump", &TestFollowCancelAdvancesWithExplicitPump});
        Tests.push_back({"Wrong-family completion fails before Core mutation", &TestWrongFamilyCompletionFailsBeforeCoreMutation});
        Tests.push_back({"Pending Follow expires while Chat is Processing", &TestPendingFollowExpiresWhileChatIsProcessing});
    }
}
