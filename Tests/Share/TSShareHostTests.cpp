#include "EventHost/TSEventExecutionHost.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestShareInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSShareInput Input = MakeShareInput("share-auto");
        const FTSShareInput Expected = Input;

        Require(Host.PostShare(Input), "First Share publication must signal");
        Input.User.UniqueId = "mutated-after-post";
        Input.User.Nickname.clear();

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedShareAdmission(Cycle, "Share Auto Pump");
        const FTSShareProcessingDispatch& Dispatch =
            RequireShareDispatch(Cycle, "Share Auto Pump");

        Require(
            Dispatch.Emission.EmissionId == EmissionId,
            "Share Auto Pump dispatch identity"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Share Auto Pump owned payload"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) == nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr,
            "Share Auto Pump must expose only a Share dispatch"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "Share Auto Pump must consume its only command"
        );

        Require(
            Host.PostShareCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Share Auto Pump completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ShareCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Share Auto Pump completion"
            ),
            EmissionId,
            "Share Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Completed sole Share must leave no dispatch or command"
        );
    }

    void TestWorkerPostShareRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(MakeShareSettings(), NowProvider);
        FTSShareInput Input = MakeShareInput("share-worker");
        const FTSShareInput Expected = Input;
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
                    bScheduleRequested = Host.PostShare(Input);
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
            "Worker Share publication must execute on a worker"
        );
        Require(
            bScheduleRequested && NowCallCount.load() == 0,
            "Worker PostShare must only publish and request owner scheduling"
        );

        Input.User.Nickname = "mutated-worker-input";
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedShareAdmission(Cycle, "Worker PostShare");
        const FTSShareProcessingDispatch& Dispatch =
            RequireShareDispatch(Cycle, "Worker PostShare");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the Share pipeline"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker Share owned payload"
        );

        Require(
            Host.PostShareCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker Share cleanup must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ShareCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Worker Share cleanup"
            ),
            EmissionId,
            "Worker Share cleanup"
        );
    }

    void TestMixedChatFollowAndSharePreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-follow");
        const FTSShareInput ShareInput = MakeShareInput("mixed-share");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must remain queued");
        Require(!Host.PostShare(ShareInput), "Mixed Share must remain queued");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Mixed Host FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Mixed Host FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Host FIFO must process Chat first and retain two commands"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Mixed Host FIFO Follow");
        Require(
            !FollowCycle.Dispatch.has_value() &&
                FollowCycle.PumpResult.has_value() &&
                FollowCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                FollowCycle.bMoreCommandsPending,
            "Host FIFO must admit Follow second behind Processing Chat"
        );

        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Mixed Host FIFO Share");
        Require(
            !ShareCycle.Dispatch.has_value() &&
                ShareCycle.PumpResult.has_value() &&
                ShareCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                !ShareCycle.bMoreCommandsPending,
            "Host FIFO must admit Share third behind Processing Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Mixed Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Mixed Chat completion"
            ),
            ChatId,
            "Mixed Chat completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(
                ChatCompletionCycle,
                "Core priority selects Follow"
            );
        Require(
            !ChatCompletionCycle.PumpResult.has_value() &&
                FollowDispatch.Emission.EmissionId == FollowId,
            "Core priority, not Host FIFO, must select Follow before Share"
        );
        RequireFollowInputEqual(
            FollowDispatch.Payload.Input,
            FollowInput,
            "Core-priority Follow payload"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Mixed Follow completion must signal"
        );
        const FTSEventHostCycleResult FollowCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                FollowCompletionCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Mixed Follow completion"
            ),
            FollowId,
            "Mixed Follow completion"
        );
        const FTSShareProcessingDispatch& ShareDispatch =
            RequireShareDispatch(
                FollowCompletionCycle,
                "Core priority advances Share"
            );
        Require(
            !FollowCompletionCycle.PumpResult.has_value() &&
                ShareDispatch.Emission.EmissionId == ShareId,
            "Confirm Auto Pump must dispatch the remaining Share"
        );
        RequireShareInputEqual(
            ShareDispatch.Payload.Input,
            ShareInput,
            "Core-priority Share payload"
        );

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Mixed Share completion must signal"
        );
        const FTSEventHostCycleResult ShareCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ShareCompletionCycle,
                ETSEventHostCommandKind::ShareCompletion,
                ShareId,
                ETSProcessingResult::Succeeded,
                "Mixed Share completion"
            ),
            ShareId,
            "Mixed Share completion"
        );
    }

    void TestShareCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("share-then-chat");

        Require(
            Host.PostShare(MakeShareInput("processing-share")),
            "Processing Share must signal"
        );
        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Processing Share");
        (void)RequireShareDispatch(ShareCycle, "Processing Share");

        Require(Host.PostChat(ChatInput), "Pending Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Pending Chat");
        Require(!ChatCycle.Dispatch.has_value(), "Chat must wait behind Share");

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Share completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ShareCompletion,
                ShareId,
                ETSProcessingResult::Succeeded,
                "Share completion captures Chat"
            ),
            ShareId,
            "Share completion captures Chat"
        );
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(CompletionCycle, "Captured Chat");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == ChatId,
            "Share Confirm Auto Pump must expose Chat in the same cycle"
        );
        RequireChatPayloadMatchesInput(
            Dispatch.Payload,
            ChatInput,
            "Captured Chat payload"
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

    void TestChatCompletionCapturesReadyShare()
    {
        FTSEventExecutionHost Host;
        const FTSShareInput ShareInput = MakeShareInput("chat-then-share");

        Require(
            Host.PostChat(MakeChatInput("processing-chat")),
            "Processing Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Processing Chat");
        (void)RequireChatDispatch(ChatCycle, "Processing Chat");

        Require(Host.PostShare(ShareInput), "Pending Share must signal");
        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Pending Share");
        Require(!ShareCycle.Dispatch.has_value(), "Share must wait behind Chat");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Chat completion captures Share"
            ),
            ChatId,
            "Chat completion captures Share"
        );
        const FTSShareProcessingDispatch& Dispatch =
            RequireShareDispatch(CompletionCycle, "Captured Share");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == ShareId,
            "Chat Confirm Auto Pump must expose Share in the same cycle"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            ShareInput,
            "Captured Share payload"
        );

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Captured Share cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::ShareCompletion,
                ShareId,
                ETSProcessingResult::Succeeded,
                "Captured Share cleanup"
            ),
            ShareId,
            "Captured Share cleanup"
        );
    }

    void TestShareCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host;
        const FTSShareInput SecondInput = MakeShareInput("share-cancel-second");

        Require(
            Host.PostShare(MakeShareInput("share-cancel-first")),
            "First cancel Share must signal"
        );
        Require(
            !Host.PostShare(SecondInput),
            "Second cancel Share must remain queued"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedShareAdmission(FirstCycle, "First cancel Share");
        (void)RequireShareDispatch(FirstCycle, "First cancel Share");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedShareAdmission(SecondCycle, "Second cancel Share");
        Require(
            !SecondCycle.Dispatch.has_value(),
            "Second Share must remain Pending before cancellation"
        );

        Require(
            Host.PostShareCompletion(FirstId, ETSProcessingResult::Cancelled),
            "First Share cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::ShareCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First Share cancellation"
            ),
            FirstId,
            "First Share cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "Share Cancel must advance through the explicit Host Pump"
        );
        const FTSShareProcessingDispatch& Dispatch =
            RequireShareDispatch(CancelCycle, "Second Share after cancellation");
        Require(
            Dispatch.Emission.EmissionId == SecondId,
            "Explicit Pump must dispatch the second Share"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            SecondInput,
            "Second Share payload"
        );

        Require(
            Host.PostShareCompletion(SecondId, ETSProcessingResult::Succeeded),
            "Second Share cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::ShareCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Second Share cleanup"
            ),
            SecondId,
            "Second Share cleanup"
        );
    }

    void TestWrongFamilyShareCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(MakeChatInput("wrong-share-family-chat")),
            "Wrong-family Chat publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Wrong-family Share completion Chat"
        );
        (void)RequireChatDispatch(
            AdmissionCycle,
            "Wrong-family Share completion Chat"
        );

        Require(
            Host.PostShareCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Wrong-family Share completion must publish"
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
            "Wrong-family Share completion must fail in the owner cycle"
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
            "Wrong-family Share recovery"
        );
        RequireConfirmedCompletion(
            Recovery,
            ChatId,
            "Wrong-family Share recovery"
        );
        Require(
            Recovery.ConfirmResult->LifecycleEvents.size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong-family Share attempt must not mutate Core or dispatch Share"
        );
    }

    void TestPendingShareExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatShareSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("share-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Share expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "Share expiry Chat");

        Require(
            Host.PostShare(MakeShareInput("expiring-share")),
            "Expiring Share publication must signal"
        );
        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Expiring Share");
        Require(
            !ShareCycle.Dispatch.has_value() &&
                ShareCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending Share must schedule expiration while Chat is Processing"
        );

        Clock.Advance(5s);
        const FTSEventHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand ==
                ETSEventHostCommandKind::None,
            "Share expiration cycle must process no command"
        );
        Require(
            ExpirationCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.EmissionId == ExpiringShareId &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.Flow == ETSEventFlow::Share &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Share must expire with its Share identity and flow"
        );
        Require(
            ExpirationCycle.PumpResult.has_value() &&
                ExpirationCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !ExpirationCycle.Dispatch.has_value(),
            "Chat must remain Processing without a Share dispatch"
        );
        Require(
            ExpirationCycle.NextWakeTime.Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Expired Share must remove the scheduled wake"
        );

        Require(
            Host.PostShare(MakeShareInput("share-after-expiry")),
            "Replacement Share must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId = RequireAcceptedShareAdmission(
            ReplacementCycle,
            "Share after expiration"
        );
        Require(
            !ReplacementCycle.Dispatch.has_value(),
            "Replacement Share must wait behind Processing Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Share expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Share expiry Chat completion"
            ),
            ChatId,
            "Share expiry Chat completion"
        );
        Require(
            RequireShareDispatch(
                ChatCompletionCycle,
                "Replacement Share dispatch"
            ).Emission.EmissionId == ReplacementId,
            "Replacement Share must dispatch after Chat completes"
        );

        Require(
            Host.PostShareCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement Share completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::ShareCompletion,
                ReplacementId,
                ETSProcessingResult::Succeeded,
                "Replacement Share cleanup"
            ),
            ReplacementId,
            "Replacement Share cleanup"
        );
    }

    void TestShareFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeShareInput("failed-share"),
            MakeShareInput("after-failed-share"),
            ETSEventHostCommandKind::ShareCompletion,
            [](FTSEventExecutionHost& Host, FTSShareInput Input)
            {
                return Host.PostShare(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostShareCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedShareAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireShareDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "Share Failed"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Share input Auto Pumps and dispatches", &TestShareInputAutoPumpsAndDispatches});
        Tests.push_back({"Worker PostShare runs on owner", &TestWorkerPostShareRunsOnOwner});
        Tests.push_back({"Mixed Chat Follow and Share preserve Host FIFO and Core order", &TestMixedChatFollowAndSharePreserveHostFifoAndCoreOrder});
        Tests.push_back({"Share completion captures ready Chat", &TestShareCompletionCapturesReadyChat});
        Tests.push_back({"Chat completion captures ready Share", &TestChatCompletionCapturesReadyShare});
        Tests.push_back({"Share cancel advances with explicit Pump", &TestShareCancelAdvancesWithExplicitPump});
        Tests.push_back({"Wrong-family Share completion fails before Core mutation", &TestWrongFamilyShareCompletionFailsBeforeCoreMutation});
        Tests.push_back({"Pending Share expires while Chat is Processing", &TestPendingShareExpiresWhileChatIsProcessing});
        Tests.push_back({"Share Failed completion is terminal and Host recovers", &TestShareFailedCompletionIsTerminalAndHostRecovers});
    }
}
