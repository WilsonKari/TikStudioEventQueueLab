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

    void MutateGiftInput(FTSGiftInput& Input)
    {
        Input.GiftId = 0;
        Input.GiftName.clear();
        Input.GiftPictureUrl.clear();
        Input.DiamondCount = 0;
        Input.RepeatCount = 0;
        Input.GiftType = 0;
        Input.Describe.clear();
        Input.bRepeatEnd = false;
        Input.GroupId.clear();
        Input.User.UniqueId = "mutated-after-post";
        Input.User.Nickname.clear();
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;
    }

    void TestGiftInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSGiftInput Input = MakeGiftInput("gift-auto");
        const FTSGiftInput Expected = Input;

        Require(Host.PostGift(Input), "First Gift publication must signal");
        MutateGiftInput(Input);

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedGiftAdmission(Cycle, "Gift Auto Pump");
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(Cycle, "Gift Auto Pump");
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Gift,
            "Gift Auto Pump dispatch identity and flow"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Gift Auto Pump owned payload"
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
                ) == nullptr &&
                std::get_if<FTSRoomUserProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr,
            "Gift Auto Pump must expose only a Gift dispatch"
        );
        Require(
            !Cycle.bMoreCommandsPending,
            "Gift Auto Pump must consume its only command"
        );

        Require(
            Host.PostGiftCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Gift Auto Pump completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::GiftCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Gift Auto Pump completion"
            ),
            EmissionId,
            "Gift Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Completed sole Gift must leave no dispatch or command"
        );
    }

    void TestWorkerPostGiftRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(MakeGiftSettings(), NowProvider);
        FTSGiftInput Input = MakeGiftInput("gift-worker");
        const FTSGiftInput Expected = Input;
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
                    bScheduleRequested = Host.PostGift(Input);
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
            "Worker Gift publication must execute on a worker"
        );
        Require(
            bScheduleRequested && NowCallCount.load() == 0,
            "Worker PostGift must only request owner scheduling"
        );

        MutateGiftInput(Input);
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedGiftAdmission(Cycle, "Worker PostGift");
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(Cycle, "Worker PostGift");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the Gift pipeline"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker Gift owned payload"
        );

        Require(
            Host.PostGiftCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker Gift cleanup must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::GiftCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Worker Gift cleanup"
            ),
            EmissionId,
            "Worker Gift cleanup"
        );
    }

    void TestMixedSixFamiliesPreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-gift-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-gift-follow");
        const FTSShareInput ShareInput = MakeShareInput("mixed-gift-share");
        const FTSLikeInput LikeInput = MakeLikeInput("mixed-gift-like");
        const FTSRoomUserInput RoomUserInput =
            MakeRoomUserInput("mixed-gift-room");
        const FTSGiftInput GiftInput = MakeGiftInput("mixed-gift");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must queue");
        Require(!Host.PostShare(ShareInput), "Mixed Share must queue");
        Require(!Host.PostLike(LikeInput), "Mixed Like must queue");
        Require(!Host.PostRoomUser(RoomUserInput), "Mixed RoomUser must queue");
        Require(!Host.PostGift(GiftInput), "Mixed Gift must queue");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Mixed FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Mixed FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Host FIFO must process Chat first"
        );
        RequireChatInputEqual(
            RequireChatDispatch(ChatCycle, "Mixed FIFO Chat").Payload.Input,
            ChatInput,
            "Mixed FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Mixed FIFO Follow");
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
                RoomUserCycle.bMoreCommandsPending,
            "Host FIFO must admit RoomUser fifth"
        );

        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Mixed FIFO Gift");
        Require(
            GiftCycle.ProcessedCommand == ETSEventHostCommandKind::GiftInput &&
                !GiftCycle.Dispatch.has_value() &&
                GiftCycle.PumpResult.has_value() &&
                GiftCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                !GiftCycle.bMoreCommandsPending,
            "Host FIFO must admit Gift sixth"
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
        const FTSGiftProcessingDispatch& GiftDispatch =
            RequireGiftDispatch(ChatCompletion, "Core selects Gift");
        Require(
            !ChatCompletion.PumpResult.has_value() &&
                GiftDispatch.Emission.EmissionId == GiftId,
            "Core priority must select Gift first"
        );
        RequireGiftInputEqual(
            GiftDispatch.Payload.Input,
            GiftInput,
            "Core-priority Gift payload"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Mixed Gift completion must signal"
        );
        const FTSEventHostCycleResult GiftCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                GiftCompletion,
                ETSEventHostCommandKind::GiftCompletion,
                GiftId,
                ETSProcessingResult::Succeeded,
                "Mixed Gift completion"
            ),
            GiftId,
            "Mixed Gift completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(GiftCompletion, "Core selects Follow");
        Require(
            !GiftCompletion.PumpResult.has_value() &&
                FollowDispatch.Emission.EmissionId == FollowId,
            "Core priority must select Follow after Gift"
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
            "Mixed six-family scenario must leave no work"
        );
    }

    void TestGiftCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("gift-then-chat");

        Require(
            Host.PostGift(MakeGiftInput("processing-gift")),
            "Processing Gift must signal"
        );
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Processing Gift");
        (void)RequireGiftDispatch(GiftCycle, "Processing Gift");

        Require(Host.PostChat(ChatInput), "Pending Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Pending Chat after Gift");
        Require(!ChatCycle.Dispatch.has_value(), "Chat must wait behind Gift");

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Gift completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::GiftCompletion,
                GiftId,
                ETSProcessingResult::Succeeded,
                "Gift completion captures Chat"
            ),
            GiftId,
            "Gift completion captures Chat"
        );
        const FTSChatProcessingDispatch& Dispatch =
            RequireChatDispatch(CompletionCycle, "Chat captured after Gift");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == ChatId,
            "Gift Confirm must expose Chat in the same cycle"
        );
        RequireChatInputEqual(
            Dispatch.Payload.Input,
            ChatInput,
            "Chat captured after Gift payload"
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

    void TestChatCompletionCapturesReadyGift()
    {
        FTSEventExecutionHost Host;
        const FTSGiftInput GiftInput = MakeGiftInput("chat-then-gift");

        Require(
            Host.PostChat(MakeChatInput("processing-chat-before-gift")),
            "Processing Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Processing Chat");
        (void)RequireChatDispatch(ChatCycle, "Processing Chat");

        Require(Host.PostGift(GiftInput), "Pending Gift must signal");
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId = RequireAcceptedGiftAdmission(
            GiftCycle,
            "Pending Gift after Chat"
        );
        Require(!GiftCycle.Dispatch.has_value(), "Gift must wait behind Chat");

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
                "Chat completion captures Gift"
            ),
            ChatId,
            "Chat completion captures Gift"
        );
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(CompletionCycle, "Gift captured after Chat");
        Require(
            !CompletionCycle.PumpResult.has_value() &&
                Dispatch.Emission.EmissionId == GiftId &&
                Dispatch.Emission.Flow == ETSEventFlow::Gift,
            "Chat Confirm must expose Gift in the same cycle"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            GiftInput,
            "Gift captured after Chat payload"
        );
        Require(
            Dispatch.Payload.Input.RepeatCount > 1 &&
                Dispatch.Payload.Input.GiftType != 0 &&
                Dispatch.Payload.Input.bRepeatEnd &&
                !Dispatch.Payload.Input.GroupId.empty(),
            "Gift repeat metadata must remain a non-trivial snapshot"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Captured Gift cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::GiftCompletion,
                GiftId,
                ETSProcessingResult::Succeeded,
                "Captured Gift cleanup"
            ),
            GiftId,
            "Captured Gift cleanup"
        );
    }

    void TestGiftCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host(MakeGiftSettings(10));
        const FTSGiftInput SecondInput = MakeGiftInput("gift-cancel-second");

        Require(
            Host.PostGift(MakeGiftInput("gift-cancel-first")),
            "First cancel Gift must signal"
        );
        Require(!Host.PostGift(SecondInput), "Second cancel Gift must queue");

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedGiftAdmission(FirstCycle, "First cancel Gift");
        (void)RequireGiftDispatch(FirstCycle, "First cancel Gift");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedGiftAdmission(SecondCycle, "Second cancel Gift");
        Require(
            !SecondCycle.Dispatch.has_value(),
            "Second Gift must remain Pending before cancellation"
        );

        Require(
            Host.PostGiftCompletion(FirstId, ETSProcessingResult::Cancelled),
            "First Gift cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::GiftCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First Gift cancellation"
            ),
            FirstId,
            "First Gift cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "Gift Cancel must advance through explicit Host Pump"
        );
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(CancelCycle, "Second Gift after cancellation");
        Require(
            Dispatch.Emission.EmissionId == SecondId,
            "Explicit Pump must dispatch the second Gift"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            SecondInput,
            "Second Gift payload"
        );

        Require(
            Host.PostGiftCompletion(SecondId, ETSProcessingResult::Succeeded),
            "Second Gift cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::GiftCompletion,
                SecondId,
                ETSProcessingResult::Succeeded,
                "Second Gift cleanup"
            ),
            SecondId,
            "Second Gift cleanup"
        );
    }

    void TestWrongFamilyGiftCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(MakeChatInput("wrong-gift-family-chat")),
            "Wrong-family Chat publication must signal"
        );
        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Wrong-family Gift completion Chat"
        );
        (void)RequireChatDispatch(
            AdmissionCycle,
            "Wrong-family Gift completion Chat"
        );

        Require(
            Host.PostGiftCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Wrong-family Gift completion must publish"
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
            "Wrong-family Gift completion must fail on owner"
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
            "Wrong-family Gift recovery"
        );
        RequireConfirmedCompletion(Recovery, ChatId, "Wrong-family Gift recovery");
        Require(
            Recovery.ConfirmResult->LifecycleEvents.size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong Gift completion must not mutate Core or dispatch"
        );
    }

    void TestPendingGiftExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatGiftSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("gift-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Gift expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "Gift expiry Chat");

        Require(
            Host.PostGift(MakeGiftInput("expiring-gift")),
            "Expiring Gift publication must signal"
        );
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringGiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Expiring Gift");
        Require(
            !GiftCycle.Dispatch.has_value() &&
                GiftCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending Gift must schedule expiration"
        );

        Clock.Advance(6s);
        const FTSGiftInput ReplacementInput =
            MakeGiftInput("gift-after-expiry");
        Require(
            Host.PostGift(ReplacementInput),
            "Replacement Gift must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId = RequireAcceptedGiftAdmission(
            ReplacementCycle,
            "Gift after expiration"
        );
        const FTSEmissionLifecycleEvents& ExpirationLifecycle =
            ReplacementCycle.DueExpirations.LifecycleEvents;
        Require(
            ExpirationLifecycle.size() == 1 &&
                ExpirationLifecycle.front().Envelope.EmissionId ==
                    ExpiringGiftId &&
                ExpirationLifecycle.front().Envelope.Flow ==
                    ETSEventFlow::Gift &&
                ExpirationLifecycle.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Replacement cycle must report the expired Gift"
        );
        Require(
            ReplacementCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::GiftInput &&
                !ReplacementCycle.Dispatch.has_value() &&
                ReplacementCycle.PumpResult.has_value() &&
                ReplacementCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy,
            "Replacement Gift must wait while Chat stays Processing"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Gift expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                ChatCompletion,
                ETSEventHostCommandKind::ChatCompletion,
                ChatId,
                ETSProcessingResult::Succeeded,
                "Gift expiry Chat completion"
            ),
            ChatId,
            "Gift expiry Chat completion"
        );
        const FTSGiftProcessingDispatch& ReplacementDispatch =
            RequireGiftDispatch(ChatCompletion, "Replacement Gift dispatch");
        Require(
            ReplacementDispatch.Emission.EmissionId == ReplacementId,
            "Replacement Gift must dispatch after Chat completes"
        );
        RequireGiftInputEqual(
            ReplacementDispatch.Payload.Input,
            ReplacementInput,
            "Replacement Gift payload"
        );

        Require(
            Host.PostGiftCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement Gift completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::GiftCompletion,
                ReplacementId,
                ETSProcessingResult::Succeeded,
                "Replacement Gift cleanup"
            ),
            ReplacementId,
            "Replacement Gift cleanup"
        );
    }

    void TestGiftFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeGiftInput("failed-gift"),
            MakeGiftInput("after-failed-gift"),
            ETSEventHostCommandKind::GiftCompletion,
            [](FTSEventExecutionHost& Host, FTSGiftInput Input)
            {
                return Host.PostGift(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostGiftCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedGiftAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireGiftDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "Gift Failed"
        );
    }

    void TestExpirationsBeforeCompletionRemainInDueExpirations()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* GiftSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Gift);
        Require(GiftSettings != nullptr, "Gift settings must exist");
        GiftSettings->TTL = 5s;

        FTSEventExecutionHost Host(Settings, Clock.MakeProvider());
        Require(
            Host.PostChat(MakeChatInput("partition-chat")),
            "Partition Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Partition Chat");
        (void)RequireChatDispatch(ChatCycle, "Partition Chat");

        Require(
            Host.PostGift(MakeGiftInput("partition-expiring-gift")),
            "Partition Gift must signal"
        );
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Partition Gift");
        Require(
            !GiftCycle.Dispatch.has_value(),
            "Partition Gift must remain Pending"
        );

        Require(
            Host.PostFollow(MakeFollowInput("partition-follow")),
            "Partition Follow must signal"
        );
        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Partition Follow");
        Require(
            !FollowCycle.Dispatch.has_value(),
            "Partition Follow must remain Pending"
        );

        Clock.Advance(6s);
        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Partition Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Envelope.EmissionId == GiftId &&
                CompletionCycle.DueExpirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Expired Gift must be reported by pre-command maintenance"
        );

        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            ETSProcessingResult::Succeeded,
            "Partition Chat completion"
        );
        RequireConfirmedCompletion(
            Completion,
            ChatId,
            "Partition Chat completion"
        );
        Require(
            Completion.ConfirmResult->LifecycleEvents.size() == 1,
            "Pre-command expiration must not be duplicated in Confirm lifecycle"
        );
        Require(
            Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == FollowId,
            "Confirm Auto Pump must select the surviving Follow"
        );
        Require(
            RequireFollowDispatch(
                CompletionCycle,
                "Partition Follow dispatch"
            ).Emission.EmissionId == FollowId,
            "Surviving Follow must dispatch in the completion cycle"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Partition Follow cleanup must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                ETSEventHostCommandKind::FollowCompletion,
                FollowId,
                ETSProcessingResult::Succeeded,
                "Partition Follow cleanup"
            ),
            FollowId,
            "Partition Follow cleanup"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Gift input Auto Pumps and dispatches",
            &TestGiftInputAutoPumpsAndDispatches
        });
        Tests.push_back({
            "Worker PostGift runs on owner",
            &TestWorkerPostGiftRunsOnOwner
        });
        Tests.push_back({
            "Mixed Chat Follow Share Like RoomUser and Gift preserve Host FIFO and Core order",
            &TestMixedSixFamiliesPreserveHostFifoAndCoreOrder
        });
        Tests.push_back({
            "Gift completion captures ready Chat",
            &TestGiftCompletionCapturesReadyChat
        });
        Tests.push_back({
            "Chat completion captures ready Gift",
            &TestChatCompletionCapturesReadyGift
        });
        Tests.push_back({
            "Gift cancel advances with explicit Pump",
            &TestGiftCancelAdvancesWithExplicitPump
        });
        Tests.push_back({
            "Wrong-family Gift completion fails before Core mutation",
            &TestWrongFamilyGiftCompletionFailsBeforeCoreMutation
        });
        Tests.push_back({
            "Pending Gift expires while Chat is Processing",
            &TestPendingGiftExpiresWhileChatIsProcessing
        });
        Tests.push_back({
            "Gift Failed completion is terminal and Host recovers",
            &TestGiftFailedCompletionIsTerminalAndHostRecovers
        });
        Tests.push_back({
            "Expirations before completion remain in DueExpirations",
            &TestExpirationsBeforeCompletionRemainInDueExpirations
        });
    }
}
