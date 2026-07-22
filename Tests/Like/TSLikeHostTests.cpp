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

    void TestLikeInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSLikeInput Input = MakeLikeInput("like-auto");
        const FTSLikeInput Expected = Input;

        Require(Host.PostLike(Input), "First Like publication must signal");
        Input.LikeCount = 0;
        Input.TotalLikeCount = 0;
        Input.User.UniqueId = "mutated-after-post";
        Input.User.Nickname.clear();

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedLikeAdmission(Cycle, "Like Auto Pump");
        const FTSLikeProcessingDispatch& Dispatch =
            RequireLikeDispatch(Cycle, "Like Auto Pump");

        Require(
            Dispatch.Emission.EmissionId == EmissionId,
            "Like Auto Pump dispatch identity"
        );
        RequireLikeInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Like Auto Pump owned payload"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSShareProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr,
            "Like Auto Pump must expose only a Like dispatch"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "Like Auto Pump must consume its only command"
        );

        Require(
            Host.PostLikeCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Like Auto Pump completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::LikeCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Like Auto Pump completion"
            ),
            EmissionId,
            "Like Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Completed sole Like must leave no dispatch or command"
        );
    }

    void TestWorkerPostLikeRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(MakeLikeSettings(), NowProvider);
        FTSLikeInput Input = MakeLikeInput("like-worker");
        const FTSLikeInput Expected = Input;
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
                    bScheduleRequested = Host.PostLike(Input);
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
            "Worker Like publication must execute on a worker"
        );
        Require(
            bScheduleRequested && NowCallCount.load() == 0,
            "Worker PostLike must only publish and request owner scheduling"
        );

        Input.LikeCount = 0;
        Input.User.Nickname = "mutated-worker-input";
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedLikeAdmission(Cycle, "Worker PostLike");
        const FTSLikeProcessingDispatch& Dispatch =
            RequireLikeDispatch(Cycle, "Worker PostLike");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the Like pipeline"
        );
        RequireLikeInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker Like owned payload"
        );

        Require(
            Host.PostLikeCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker Like cleanup must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::LikeCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Worker Like cleanup"
            ),
            EmissionId,
            "Worker Like cleanup"
        );
    }

    void TestMixedChatFollowShareAndLikePreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-like-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-like-follow");
        const FTSShareInput ShareInput = MakeShareInput("mixed-like-share");
        const FTSLikeInput LikeInput = MakeLikeInput("mixed-like");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must remain queued");
        Require(!Host.PostShare(ShareInput), "Mixed Share must remain queued");
        Require(!Host.PostLike(LikeInput), "Mixed Like must remain queued");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Mixed Host FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Mixed Host FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Host FIFO must process Chat first and retain three commands"
        );
        RequireChatPayloadMatchesInput(
            RequireChatDispatch(ChatCycle, "Mixed Host FIFO Chat").Payload,
            ChatInput,
            "Mixed Host FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId = RequireAcceptedFollowAdmission(
            FollowCycle,
            "Mixed Host FIFO Follow"
        );
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
                ShareCycle.bMoreCommandsPending,
            "Host FIFO must admit Share third behind Processing Chat"
        );

        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Mixed Host FIFO Like");
        Require(
            !LikeCycle.Dispatch.has_value() &&
                LikeCycle.PumpResult.has_value() &&
                LikeCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                !LikeCycle.bMoreCommandsPending,
            "Host FIFO must admit Like fourth behind Processing Chat"
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
            "Core priority, not Host FIFO, must select Follow next"
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
                "Core priority selects Share"
            );
        Require(
            !FollowCompletionCycle.PumpResult.has_value() &&
                ShareDispatch.Emission.EmissionId == ShareId,
            "Core priority must select Share before Like"
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
        const FTSLikeProcessingDispatch& LikeDispatch = RequireLikeDispatch(
            ShareCompletionCycle,
            "Core priority advances Like"
        );
        Require(
            !ShareCompletionCycle.PumpResult.has_value() &&
                LikeDispatch.Emission.EmissionId == LikeId,
            "Confirm Auto Pump must dispatch the remaining Like"
        );
        RequireLikeInputEqual(
            LikeDispatch.Payload.Input,
            LikeInput,
            "Core-priority Like payload"
        );

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Mixed Like completion must signal"
        );
        const FTSEventHostCycleResult LikeCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                LikeCompletionCycle,
                ETSEventHostCommandKind::LikeCompletion,
                LikeId,
                ETSProcessingResult::Succeeded,
                "Mixed Like completion"
            ),
            LikeId,
            "Mixed Like completion"
        );
    }

    void TestLikeCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("like-then-chat");

        Require(
            Host.PostLike(MakeLikeInput("processing-like")),
            "Processing Like must signal"
        );
        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Processing Like");
        (void)RequireLikeDispatch(LikeCycle, "Processing Like");

        Require(Host.PostChat(ChatInput), "Pending Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Pending Chat after Like");
        Require(!ChatCycle.Dispatch.has_value(), "Chat must wait behind Like");

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Like completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::LikeCompletion,
                LikeId,
                ETSProcessingResult::Succeeded,
                "Like completion captures Chat"
            ),
            LikeId,
            "Like completion captures Chat"
        );
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(CompletionCycle, "Chat captured after Like");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == ChatId,
            "Like Confirm Auto Pump must expose Chat in the same cycle"
        );
        RequireChatPayloadMatchesInput(
            Dispatch.Payload,
            ChatInput,
            "Chat captured after Like payload"
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

    void TestChatCompletionCapturesReadyLike()
    {
        FTSEventExecutionHost Host;
        const FTSLikeInput LikeInput = MakeLikeInput("chat-then-like");

        Require(
            Host.PostChat(MakeChatInput("processing-chat-before-like")),
            "Processing Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Processing Chat");
        (void)RequireChatDispatch(ChatCycle, "Processing Chat");

        Require(Host.PostLike(LikeInput), "Pending Like must signal");
        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Pending Like after Chat");
        Require(!LikeCycle.Dispatch.has_value(), "Like must wait behind Chat");

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
                "Chat completion captures Like"
            ),
            ChatId,
            "Chat completion captures Like"
        );
        const FTSLikeProcessingDispatch& Dispatch =
            RequireLikeDispatch(CompletionCycle, "Like captured after Chat");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == LikeId,
            "Chat Confirm Auto Pump must expose Like in the same cycle"
        );
        RequireLikeInputEqual(
            Dispatch.Payload.Input,
            LikeInput,
            "Like captured after Chat payload"
        );

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Captured Like cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::LikeCompletion,
                LikeId,
                ETSProcessingResult::Succeeded,
                "Captured Like cleanup"
            ),
            LikeId,
            "Captured Like cleanup"
        );
    }

    void TestLikeCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host(MakeLikeSettings(10));
        const FTSLikeInput SecondInput = MakeLikeInput("like-cancel-second");

        Require(
            Host.PostLike(MakeLikeInput("like-cancel-first")),
            "First cancel Like must signal"
        );
        Require(
            !Host.PostLike(SecondInput),
            "Second cancel Like must remain queued"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedLikeAdmission(FirstCycle, "First cancel Like");
        (void)RequireLikeDispatch(FirstCycle, "First cancel Like");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedLikeAdmission(SecondCycle, "Second cancel Like");
        Require(
            !SecondCycle.Dispatch.has_value(),
            "Second Like must remain Pending before cancellation"
        );

        Require(
            Host.PostLikeCompletion(FirstId, ETSProcessingResult::Cancelled),
            "First Like cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::LikeCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First Like cancellation"
            ),
            FirstId,
            "First Like cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "Like Cancel must advance through the explicit Host Pump"
        );
        const FTSLikeProcessingDispatch& Dispatch =
            RequireLikeDispatch(CancelCycle, "Second Like after cancellation");
        Require(
            Dispatch.Emission.EmissionId == SecondId,
            "Explicit Pump must dispatch the second Like"
        );
        RequireLikeInputEqual(
            Dispatch.Payload.Input,
            SecondInput,
            "Second Like payload"
        );

        Require(
            Host.PostLikeCompletion(SecondId, ETSProcessingResult::Succeeded),
            "Second Like cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::LikeCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Second Like cleanup"
            ),
            SecondId,
            "Second Like cleanup"
        );
    }

    void TestWrongFamilyLikeCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(MakeChatInput("wrong-like-family-chat")),
            "Wrong-family Chat publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Wrong-family Like completion Chat"
        );
        (void)RequireChatDispatch(
            AdmissionCycle,
            "Wrong-family Like completion Chat"
        );

        Require(
            Host.PostLikeCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Wrong-family Like completion must publish"
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
            "Wrong-family Like completion must fail in the owner cycle"
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
            "Wrong-family Like recovery"
        );
        RequireConfirmedCompletion(
            Recovery,
            ChatId,
            "Wrong-family Like recovery"
        );
        Require(
            Recovery.ConfirmResult->LifecycleEvents.size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong-family Like attempt must not mutate Core or dispatch Like"
        );
    }

    void TestPendingLikeExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatLikeSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("like-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Like expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "Like expiry Chat");

        Require(
            Host.PostLike(MakeLikeInput("expiring-like")),
            "Expiring Like publication must signal"
        );
        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringLikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Expiring Like");
        Require(
            !LikeCycle.Dispatch.has_value() &&
                LikeCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending Like must schedule expiration while Chat is Processing"
        );

        Clock.Advance(5s);
        const FTSEventHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand == ETSEventHostCommandKind::None,
            "Like expiration cycle must process no command"
        );
        Require(
            ExpirationCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.EmissionId == ExpiringLikeId &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.Flow == ETSEventFlow::Like &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Like must expire with its Like identity and flow"
        );
        Require(
            ExpirationCycle.PumpResult.has_value() &&
                ExpirationCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !ExpirationCycle.Dispatch.has_value(),
            "Chat must remain Processing without a Like dispatch"
        );
        Require(
            ExpirationCycle.NextWakeTime.Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Expired Like must remove the scheduled wake"
        );

        Require(
            Host.PostLike(MakeLikeInput("like-after-expiry")),
            "Replacement Like must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId = RequireAcceptedLikeAdmission(
            ReplacementCycle,
            "Like after expiration"
        );
        Require(
            !ReplacementCycle.Dispatch.has_value(),
            "Replacement Like must wait behind Processing Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Like expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletionCycle,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Like expiry Chat completion"
            ),
            ChatId,
            "Like expiry Chat completion"
        );
        Require(
            RequireLikeDispatch(
                ChatCompletionCycle,
                "Replacement Like dispatch"
            ).Emission.EmissionId == ReplacementId,
            "Replacement Like must dispatch after Chat completes"
        );

        Require(
            Host.PostLikeCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement Like completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::LikeCompletion,
                ReplacementId,
                ETSProcessingResult::Succeeded,
                "Replacement Like cleanup"
            ),
            ReplacementId,
            "Replacement Like cleanup"
        );
    }

    void TestLikeFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeLikeInput("failed-like"),
            MakeLikeInput("after-failed-like"),
            ETSEventHostCommandKind::LikeCompletion,
            [](FTSEventExecutionHost& Host, FTSLikeInput Input)
            {
                return Host.PostLike(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostLikeCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedLikeAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireLikeDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "Like Failed"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikeHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Like input Auto Pumps and dispatches", &TestLikeInputAutoPumpsAndDispatches});
        Tests.push_back({"Worker PostLike runs on owner", &TestWorkerPostLikeRunsOnOwner});
        Tests.push_back({"Mixed Chat Follow Share and Like preserve Host FIFO and Core order", &TestMixedChatFollowShareAndLikePreserveHostFifoAndCoreOrder});
        Tests.push_back({"Like completion captures ready Chat", &TestLikeCompletionCapturesReadyChat});
        Tests.push_back({"Chat completion captures ready Like", &TestChatCompletionCapturesReadyLike});
        Tests.push_back({"Like cancel advances with explicit Pump", &TestLikeCancelAdvancesWithExplicitPump});
        Tests.push_back({"Wrong-family Like completion fails before Core mutation", &TestWrongFamilyLikeCompletionFailsBeforeCoreMutation});
        Tests.push_back({"Pending Like expires while Chat is Processing", &TestPendingLikeExpiresWhileChatIsProcessing});
        Tests.push_back({"Like Failed completion is terminal and Host recovers", &TestLikeFailedCompletionIsTerminalAndHostRecovers});
    }
}
