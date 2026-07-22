#include "EventHost/TSEventExecutionHost.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestRoomUserInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSRoomUserInput Input = MakeRoomUserInput("room-auto");
        const FTSRoomUserInput Expected = Input;

        Require(
            Host.PostRoomUser(Input),
            "First RoomUser publication must signal"
        );
        Input.ViewerCount = 0;
        Input.TopGifterRank = 0;
        Input.TopViewers[0].UniqueId = "mutated-after-post";
        Input.TopViewers[0].Nickname.clear();
        Input.TopViewers[0].ProfilePictureUrl.clear();
        Input.TopViewers[0].CoinCount = 0;
        Input.TopViewers[0].bIsModerator = false;
        Input.TopViewers[0].bIsSubscriber = true;
        Input.TopViewers[0].GifterLevel = 0;
        Input.TopViewers[0].TeamMemberLevel = 0;
        std::swap(Input.TopViewers[0], Input.TopViewers[1]);
        Input.TopViewers.pop_back();

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedRoomUserAdmission(
            Cycle,
            "RoomUser Auto Pump"
        );
        const FTSRoomUserProcessingDispatch& Dispatch =
            RequireRoomUserDispatch(Cycle, "RoomUser Auto Pump");
        Require(
            Dispatch.Emission.EmissionId == EmissionId,
            "RoomUser Auto Pump dispatch identity"
        );
        RequireRoomUserInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "RoomUser Auto Pump owned payload"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSShareProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSLikeProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr,
            "RoomUser Auto Pump must expose only a RoomUser dispatch"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "RoomUser Auto Pump must consume its only command"
        );

        Require(
            Host.PostRoomUserCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "RoomUser Auto Pump completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "RoomUser Auto Pump completion"
            ),
            EmissionId,
            "RoomUser Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Completed sole RoomUser must leave no dispatch or command"
        );
    }

    void TestWorkerPostRoomUserRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(MakeRoomUserSettings(), NowProvider);
        FTSRoomUserInput Input = MakeRoomUserInput("room-worker");
        const FTSRoomUserInput Expected = Input;
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
                    bScheduleRequested = Host.PostRoomUser(Input);
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
            "Worker RoomUser publication must execute on a worker"
        );
        Require(
            bScheduleRequested && NowCallCount.load() == 0,
            "Worker PostRoomUser must only request owner scheduling"
        );

        Input.ViewerCount = 0;
        Input.TopViewers[0].Nickname = "mutated-worker-input";
        Input.TopViewers.pop_back();
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedRoomUserAdmission(
            Cycle,
            "Worker PostRoomUser"
        );
        const FTSRoomUserProcessingDispatch& Dispatch =
            RequireRoomUserDispatch(Cycle, "Worker PostRoomUser");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the RoomUser pipeline"
        );
        RequireRoomUserInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker RoomUser owned payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker RoomUser cleanup must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Worker RoomUser cleanup"
            ),
            EmissionId,
            "Worker RoomUser cleanup"
        );
    }

    void TestMixedFiveFamiliesPreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-room-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-room-follow");
        const FTSShareInput ShareInput = MakeShareInput("mixed-room-share");
        const FTSLikeInput LikeInput = MakeLikeInput("mixed-room-like");
        const FTSRoomUserInput RoomUserInput =
            MakeRoomUserInput("mixed-room-user");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must queue");
        Require(!Host.PostShare(ShareInput), "Mixed Share must queue");
        Require(!Host.PostLike(LikeInput), "Mixed Like must queue");
        Require(!Host.PostRoomUser(RoomUserInput), "Mixed RoomUser must queue");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Mixed FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Mixed FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Host FIFO must process Chat first"
        );
        RequireChatPayloadMatchesInput(
            RequireChatDispatch(ChatCycle, "Mixed FIFO Chat").Payload,
            ChatInput,
            "Mixed FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId = RequireAcceptedFollowAdmission(
            FollowCycle,
            "Mixed FIFO Follow"
        );
        Require(
            FollowCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::FollowInput &&
                !FollowCycle.Dispatch.has_value() &&
                FollowCycle.PumpResult.has_value() &&
                FollowCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                FollowCycle.bMoreCommandsPending,
            "Host FIFO must admit Follow second"
        );

        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Mixed FIFO Share");
        Require(
            ShareCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::ShareInput &&
                !ShareCycle.Dispatch.has_value() &&
                ShareCycle.PumpResult.has_value() &&
                ShareCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                ShareCycle.bMoreCommandsPending,
            "Host FIFO must admit Share third"
        );

        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Mixed FIFO Like");
        Require(
            LikeCycle.ProcessedCommand == ETSEventHostCommandKind::LikeInput &&
                !LikeCycle.Dispatch.has_value() &&
                LikeCycle.PumpResult.has_value() &&
                LikeCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                LikeCycle.bMoreCommandsPending,
            "Host FIFO must admit Like fourth"
        );

        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Mixed FIFO RoomUser"
        );
        Require(
            RoomUserCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::RoomUserInput &&
                !RoomUserCycle.Dispatch.has_value() &&
                RoomUserCycle.PumpResult.has_value() &&
                RoomUserCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !RoomUserCycle.bMoreCommandsPending,
            "Host FIFO must admit RoomUser fifth"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Mixed Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletion,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Mixed Chat completion"
            ),
            ChatId,
            "Mixed Chat completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(ChatCompletion, "Core selects Follow");
        Require(
            !ChatCompletion.PumpResult.has_value() &&
                FollowDispatch.Emission.EmissionId == FollowId,
            "Core priority, not Host FIFO, must select Follow"
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
        const FTSEventHostCycleResult FollowCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                FollowCompletion,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Mixed Follow completion"
            ),
            FollowId,
            "Mixed Follow completion"
        );
        const FTSShareProcessingDispatch& ShareDispatch =
            RequireShareDispatch(FollowCompletion, "Core selects Share");
        Require(
            !FollowCompletion.PumpResult.has_value() &&
                ShareDispatch.Emission.EmissionId == ShareId,
            "Core priority must select Share after Follow"
        );
        RequireShareInputEqual(
            ShareDispatch.Payload.Input,
            ShareInput,
            "Core-priority Share payload"
        );

        Require(
            Host.PostShareCompletion(
                ShareId,
                ETSProcessingResult::Succeeded
            ),
            "Mixed Share completion must signal"
        );
        const FTSEventHostCycleResult ShareCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ShareCompletion,
                ETSEventHostCommandKind::ShareCompletion,
                ShareId,
                ETSProcessingResult::Succeeded,
                "Mixed Share completion"
            ),
            ShareId,
            "Mixed Share completion"
        );
        const FTSRoomUserProcessingDispatch& RoomUserDispatch =
            RequireRoomUserDispatch(
                ShareCompletion,
                "Core selects RoomUser"
            );
        Require(
            !ShareCompletion.PumpResult.has_value() &&
                RoomUserDispatch.Emission.EmissionId == RoomUserId,
            "Core priority must select RoomUser before Like"
        );
        RequireRoomUserInputEqual(
            RoomUserDispatch.Payload.Input,
            RoomUserInput,
            "Core-priority RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "Mixed RoomUser completion must signal"
        );
        const FTSEventHostCycleResult RoomUserCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                RoomUserCompletion,
                ETSEventHostCommandKind::RoomUserCompletion,
                RoomUserId,
                ETSProcessingResult::Succeeded,
                "Mixed RoomUser completion"
            ),
            RoomUserId,
            "Mixed RoomUser completion"
        );
        const FTSLikeProcessingDispatch& LikeDispatch =
            RequireLikeDispatch(RoomUserCompletion, "Core selects Like");
        Require(
            !RoomUserCompletion.PumpResult.has_value() &&
                LikeDispatch.Emission.EmissionId == LikeId,
            "Core priority must select Like after RoomUser"
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
        const FTSEventHostCycleResult LikeCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                LikeCompletion,
                ETSEventHostCommandKind::LikeCompletion,
                LikeId,
                ETSProcessingResult::Succeeded,
                "Mixed Like completion"
            ),
            LikeId,
            "Mixed Like completion"
        );
        Require(
            !LikeCompletion.Dispatch.has_value() &&
                !LikeCompletion.bMoreCommandsPending,
            "Mixed five-family scenario must leave no work"
        );
    }

    void TestRoomUserCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("room-then-chat");

        Require(
            Host.PostRoomUser(MakeRoomUserInput("processing-room")),
            "Processing RoomUser must signal"
        );
        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Processing RoomUser"
        );
        (void)RequireRoomUserDispatch(RoomUserCycle, "Processing RoomUser");

        Require(Host.PostChat(ChatInput), "Pending Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            ChatCycle,
            "Pending Chat after RoomUser"
        );
        Require(!ChatCycle.Dispatch.has_value(), "Chat must wait behind RoomUser");

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "RoomUser completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                RoomUserId,
                ETSProcessingResult::Succeeded,
                "RoomUser completion captures Chat"
            ),
            RoomUserId,
            "RoomUser completion captures Chat"
        );
        const FTSChatProcessingDispatch& Dispatch = RequireChatDispatch(
            CompletionCycle,
            "Chat captured after RoomUser"
        );
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == ChatId,
            "RoomUser Confirm must expose Chat in the same cycle"
        );
        RequireChatPayloadMatchesInput(
            Dispatch.Payload,
            ChatInput,
            "Chat captured after RoomUser payload"
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

    void TestChatCompletionCapturesReadyRoomUser()
    {
        FTSEventExecutionHost Host;
        const FTSRoomUserInput RoomUserInput =
            MakeRoomUserInput("chat-then-room");

        Require(
            Host.PostChat(MakeChatInput("processing-chat-before-room")),
            "Processing Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Processing Chat");
        (void)RequireChatDispatch(ChatCycle, "Processing Chat");

        Require(
            Host.PostRoomUser(RoomUserInput),
            "Pending RoomUser must signal"
        );
        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Pending RoomUser after Chat"
        );
        Require(
            !RoomUserCycle.Dispatch.has_value(),
            "RoomUser must wait behind Chat"
        );

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
                "Chat completion captures RoomUser"
            ),
            ChatId,
            "Chat completion captures RoomUser"
        );
        const FTSRoomUserProcessingDispatch& Dispatch =
            RequireRoomUserDispatch(
                CompletionCycle,
                "RoomUser captured after Chat"
            );
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == RoomUserId,
            "Chat Confirm must expose RoomUser in the same cycle"
        );
        RequireRoomUserInputEqual(
            Dispatch.Payload.Input,
            RoomUserInput,
            "RoomUser captured after Chat payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "Captured RoomUser cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                RoomUserId,
                ETSProcessingResult::Succeeded,
                "Captured RoomUser cleanup"
            ),
            RoomUserId,
            "Captured RoomUser cleanup"
        );
    }

    void TestRoomUserCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host(MakeRoomUserSettings(10));
        const FTSRoomUserInput SecondInput =
            MakeRoomUserInput("room-cancel-second");

        Require(
            Host.PostRoomUser(MakeRoomUserInput("room-cancel-first")),
            "First cancel RoomUser must signal"
        );
        Require(
            !Host.PostRoomUser(SecondInput),
            "Second cancel RoomUser must remain queued"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId = RequireAcceptedRoomUserAdmission(
            FirstCycle,
            "First cancel RoomUser"
        );
        (void)RequireRoomUserDispatch(FirstCycle, "First cancel RoomUser");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId = RequireAcceptedRoomUserAdmission(
            SecondCycle,
            "Second cancel RoomUser"
        );
        Require(
            !SecondCycle.Dispatch.has_value(),
            "Second RoomUser must remain Pending before cancellation"
        );

        Require(
            Host.PostRoomUserCompletion(
                FirstId,
                ETSProcessingResult::Cancelled
            ),
            "First RoomUser cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First RoomUser cancellation"
            ),
            FirstId,
            "First RoomUser cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "RoomUser Cancel must advance through explicit Host Pump"
        );
        const FTSRoomUserProcessingDispatch& Dispatch =
            RequireRoomUserDispatch(
                CancelCycle,
                "Second RoomUser after cancellation"
            );
        Require(
            Dispatch.Emission.EmissionId == SecondId,
            "Explicit Pump must dispatch the second RoomUser"
        );
        RequireRoomUserInputEqual(
            Dispatch.Payload.Input,
            SecondInput,
            "Second RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                SecondId,
                ETSProcessingResult::Succeeded
            ),
            "Second RoomUser cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Second RoomUser cleanup"
            ),
            SecondId,
            "Second RoomUser cleanup"
        );
    }

    void TestWrongFamilyRoomUserCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(MakeChatInput("wrong-room-family-chat")),
            "Wrong-family Chat publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Wrong-family RoomUser completion Chat"
        );
        (void)RequireChatDispatch(
            AdmissionCycle,
            "Wrong-family RoomUser completion Chat"
        );

        Require(
            Host.PostRoomUserCompletion(
                ChatId,
                ETSProcessingResult::Succeeded
            ),
            "Wrong-family RoomUser completion must publish"
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
            "Wrong-family RoomUser completion must fail on owner"
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
            "Wrong-family RoomUser recovery"
        );
        RequireConfirmedCompletion(
            Recovery,
            ChatId,
            "Wrong-family RoomUser recovery"
        );
        Require(
            Recovery.ConfirmResult->LifecycleEvents.size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong RoomUser completion must not mutate Core or dispatch"
        );
    }

    void TestPendingRoomUserExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatRoomUserSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("room-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "RoomUser expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "RoomUser expiry Chat");

        Require(
            Host.PostRoomUser(MakeRoomUserInput("expiring-room")),
            "Expiring RoomUser publication must signal"
        );
        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringRoomUserId =
            RequireAcceptedRoomUserAdmission(
                RoomUserCycle,
                "Expiring RoomUser"
            );
        Require(
            !RoomUserCycle.Dispatch.has_value() &&
                RoomUserCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending RoomUser must schedule expiration"
        );

        Clock.Advance(6s);
        const FTSEventHostCycleResult ExpirationCycle = Host.RunOneCycle();
        Require(
            ExpirationCycle.ProcessedCommand == ETSEventHostCommandKind::None,
            "RoomUser expiration cycle must process no command"
        );
        Require(
            ExpirationCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.EmissionId == ExpiringRoomUserId &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0]
                        .Envelope.Flow == ETSEventFlow::RoomUser &&
                ExpirationCycle.DueExpirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending RoomUser expiration identity and flow mismatch"
        );
        Require(
            ExpirationCycle.PumpResult.has_value() &&
                ExpirationCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !ExpirationCycle.Dispatch.has_value(),
            "Chat must remain Processing without RoomUser dispatch"
        );
        Require(
            ExpirationCycle.NextWakeTime.Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Expired RoomUser must remove its scheduled wake"
        );

        const FTSRoomUserInput ReplacementInput =
            MakeRoomUserInput("room-after-expiry");
        Require(
            Host.PostRoomUser(ReplacementInput),
            "Replacement RoomUser must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId = RequireAcceptedRoomUserAdmission(
            ReplacementCycle,
            "RoomUser after expiration"
        );
        Require(
            !ReplacementCycle.Dispatch.has_value(),
            "Replacement RoomUser must wait behind Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "RoomUser expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletion,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "RoomUser expiry Chat completion"
            ),
            ChatId,
            "RoomUser expiry Chat completion"
        );
        const FTSRoomUserProcessingDispatch& ReplacementDispatch =
            RequireRoomUserDispatch(
                ChatCompletion,
                "Replacement RoomUser dispatch"
            );
        Require(
            ReplacementDispatch.Emission.EmissionId == ReplacementId,
            "Replacement RoomUser must dispatch after Chat completes"
        );
        RequireRoomUserInputEqual(
            ReplacementDispatch.Payload.Input,
            ReplacementInput,
            "Replacement RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement RoomUser completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::RoomUserCompletion,
                ReplacementId,
                ETSProcessingResult::Succeeded,
                "Replacement RoomUser cleanup"
            ),
            ReplacementId,
            "Replacement RoomUser cleanup"
        );
    }

    void TestRoomUserFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeRoomUserInput("failed-room-user"),
            MakeRoomUserInput("after-failed-room-user"),
            ETSEventHostCommandKind::RoomUserCompletion,
            [](FTSEventExecutionHost& Host, FTSRoomUserInput Input)
            {
                return Host.PostRoomUser(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostRoomUserCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedRoomUserAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireRoomUserDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "RoomUser Failed"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterRoomUserHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "RoomUser input Auto Pumps and dispatches",
            &TestRoomUserInputAutoPumpsAndDispatches
        });
        Tests.push_back({
            "Worker PostRoomUser runs on owner",
            &TestWorkerPostRoomUserRunsOnOwner
        });
        Tests.push_back({
            "Mixed Chat Follow Share Like and RoomUser preserve Host FIFO and Core order",
            &TestMixedFiveFamiliesPreserveHostFifoAndCoreOrder
        });
        Tests.push_back({
            "RoomUser completion captures ready Chat",
            &TestRoomUserCompletionCapturesReadyChat
        });
        Tests.push_back({
            "Chat completion captures ready RoomUser",
            &TestChatCompletionCapturesReadyRoomUser
        });
        Tests.push_back({
            "RoomUser cancel advances with explicit Pump",
            &TestRoomUserCancelAdvancesWithExplicitPump
        });
        Tests.push_back({
            "Wrong-family RoomUser completion fails before Core mutation",
            &TestWrongFamilyRoomUserCompletionFailsBeforeCoreMutation
        });
        Tests.push_back({
            "Pending RoomUser expires while Chat is Processing",
            &TestPendingRoomUserExpiresWhileChatIsProcessing
        });
        Tests.push_back({
            "RoomUser Failed completion is terminal and Host recovers",
            &TestRoomUserFailedCompletionIsTerminalAndHostRecovers
        });
    }
}
