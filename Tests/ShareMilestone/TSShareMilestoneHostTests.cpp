#include "EventHost/TSEventExecutionHost.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void MutateShareInput(FTSShareInput& Input)
    {
        Input.User.UniqueId = "mutated";
        Input.User.Nickname = "mutated";
        Input.User.ProfilePictureUrl = "mutated";
        Input.User.FollowRole = -1;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = -1;
        Input.User.GifterLevel = -1;
        Input.User.TeamMemberLevel = -1;
    }

    void RequireBusyPendingCycle(
        const FTSEventHostCycleResult& Cycle,
        bool bExpectedMoreCommands,
        const std::string& Context
    )
    {
        Require(
            !Cycle.Dispatch.has_value() &&
                Cycle.PumpResult.has_value() &&
                Cycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy &&
                Cycle.bMoreCommandsPending == bExpectedMoreCommands,
            Context + ": pending behind InFlight"
        );
    }

    void RequireSuccessfulCompletion(
        const FTSEventHostCycleResult& Cycle,
        ETSEventHostCommandKind ExpectedCommand,
        FTSEmissionId EmissionId,
        const std::string& Context
    )
    {
        RequireConfirmedCompletion(
            RequireCompletion(
                Cycle,
                ExpectedCommand,
                EmissionId,
                ETSProcessingResult::Succeeded,
                Context
            ),
            EmissionId,
            Context
        );
    }

    void TestShareMilestoneInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSShareInput Input = MakeShareInput("share-milestone-auto");
        const FTSShareInput Expected = Input;

        Require(
            Host.PostShareMilestone(Input),
            "First ShareMilestone publication must signal"
        );
        MutateShareInput(Input);

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedShareMilestoneAdmission(
                Cycle,
                "ShareMilestone Auto Pump"
            );
        const FTSShareMilestoneProcessingDispatch& Dispatch =
            RequireShareMilestoneDispatch(
                Cycle,
                "ShareMilestone Auto Pump"
            );
        Require(
            Dispatch.Emission.EmissionId == EmissionId,
            "ShareMilestone Auto Pump identity"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "ShareMilestone Auto Pump snapshot"
        );
        Require(
            std::get_if<FTSShareProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSLikeProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSRoomUserProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSGiftProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSGiftComboProcessingDispatch>(
                    &*Cycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSMemberProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                !Cycle.bMoreCommandsPending,
            "ShareMilestone dispatch must contain no other route"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "ShareMilestone completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            EmissionId,
            "ShareMilestone Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "ShareMilestone completion must clean all work"
        );
    }

    void TestWorkerPostShareMilestoneRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(FTSEventQueueSettings{}, NowProvider);
        const FTSShareInput Input =
            MakeShareInput("share-milestone-worker");
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
                    bScheduleRequested = Host.PostShareMilestone(Input);
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
            WorkerThreadId != OwnerThreadId &&
                bScheduleRequested &&
                NowCallCount.load() == 0,
            "Worker ShareMilestone publication must only enqueue"
        );
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedShareMilestoneAdmission(
                Cycle,
                "Worker ShareMilestone"
            );
        Require(
            NowCallCount.load() > 0,
            "Owner cycle must execute the ShareMilestone clock"
        );
        RequireShareInputEqual(
            RequireShareMilestoneDispatch(
                Cycle,
                "Worker ShareMilestone"
            ).Payload.Input,
            Input,
            "Worker ShareMilestone snapshot"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker ShareMilestone cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            EmissionId,
            "Worker ShareMilestone cleanup"
        );
    }

    void TestNineRoutesPreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("nine-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("nine-follow");
        const FTSShareInput ShareInput = MakeShareInput("nine-share");
        const FTSShareInput MilestoneInput =
            MakeShareInput("nine-milestone");
        const FTSLikeInput LikeInput = MakeLikeInput("nine-like");
        const FTSRoomUserInput RoomUserInput =
            MakeRoomUserInput("nine-room");
        const FTSGiftInput GiftInput = MakeGiftInput("nine-gift");
        const FTSGiftInput ComboInput = MakeGiftInput("nine-combo");
        const FTSMemberInput MemberInput = MakeMemberInput("nine-member");

        Require(Host.PostChat(ChatInput), "Nine Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Nine Follow must queue");
        Require(!Host.PostShare(ShareInput), "Nine Share must queue");
        Require(
            !Host.PostShareMilestone(MilestoneInput),
            "Nine ShareMilestone must queue"
        );
        Require(!Host.PostLike(LikeInput), "Nine Like must queue");
        Require(
            !Host.PostRoomUser(RoomUserInput),
            "Nine RoomUser must queue"
        );
        Require(!Host.PostGift(GiftInput), "Nine Gift must queue");
        Require(!Host.PostGiftCombo(ComboInput), "Nine GiftCombo must queue");
        Require(!Host.PostMember(MemberInput), "Nine Member must queue");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Nine FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Nine FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Nine-route FIFO must process Chat first"
        );
        RequireChatPayloadMatchesInput(
            RequireChatDispatch(ChatCycle, "Nine FIFO Chat").Payload,
            ChatInput,
            "Nine FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Nine FIFO Follow");
        RequireBusyPendingCycle(FollowCycle, true, "Follow admitted second");

        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Nine FIFO Share");
        RequireBusyPendingCycle(ShareCycle, true, "Share admitted third");

        const FTSEventHostCycleResult MilestoneCycle = Host.RunOneCycle();
        const FTSEmissionId MilestoneId =
            RequireAcceptedShareMilestoneAdmission(
                MilestoneCycle,
                "Nine FIFO ShareMilestone"
            );
        RequireBusyPendingCycle(
            MilestoneCycle,
            true,
            "ShareMilestone admitted fourth"
        );

        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Nine FIFO Like");
        RequireBusyPendingCycle(LikeCycle, true, "Like admitted fifth");

        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Nine FIFO RoomUser"
        );
        RequireBusyPendingCycle(
            RoomUserCycle,
            true,
            "RoomUser admitted sixth"
        );

        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Nine FIFO Gift");
        RequireBusyPendingCycle(GiftCycle, true, "Gift admitted seventh");

        const FTSEventHostCycleResult ComboCycle = Host.RunOneCycle();
        const FTSEmissionId ComboId = RequireAcceptedGiftComboAdmission(
            ComboCycle,
            "Nine FIFO GiftCombo"
        );
        RequireBusyPendingCycle(
            ComboCycle,
            true,
            "GiftCombo admitted eighth"
        );

        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(MemberCycle, "Nine FIFO Member");
        RequireBusyPendingCycle(MemberCycle, false, "Member admitted ninth");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Nine Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ChatCompletion,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Nine Chat completion"
        );
        const FTSGiftComboProcessingDispatch& ComboDispatch =
            RequireGiftComboDispatch(
                ChatCompletion,
                "Core selects GiftCombo"
            );
        Require(
            ComboDispatch.Emission.EmissionId == ComboId,
            "GiftCombo priority"
        );
        RequireGiftInputEqual(
            ComboDispatch.Payload.Input,
            ComboInput,
            "Nine GiftCombo payload"
        );

        Require(
            Host.PostGiftComboCompletion(
                ComboId,
                ETSProcessingResult::Succeeded
            ),
            "Nine GiftCombo completion must signal"
        );
        const FTSEventHostCycleResult ComboCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ComboCompletion,
            ETSEventHostCommandKind::GiftComboCompletion,
            ComboId,
            "Nine GiftCombo completion"
        );
        const FTSGiftProcessingDispatch& GiftDispatch =
            RequireGiftDispatch(ComboCompletion, "Core selects Gift");
        Require(GiftDispatch.Emission.EmissionId == GiftId, "Gift priority");
        RequireGiftInputEqual(
            GiftDispatch.Payload.Input,
            GiftInput,
            "Nine Gift payload"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Nine Gift completion must signal"
        );
        const FTSEventHostCycleResult GiftCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            GiftCompletion,
            ETSEventHostCommandKind::GiftCompletion,
            GiftId,
            "Nine Gift completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(GiftCompletion, "Core selects Follow");
        Require(
            FollowDispatch.Emission.EmissionId == FollowId,
            "Follow priority"
        );
        RequireFollowInputEqual(
            FollowDispatch.Payload.Input,
            FollowInput,
            "Nine Follow payload"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Nine Follow completion must signal"
        );
        const FTSEventHostCycleResult FollowCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            FollowCompletion,
            ETSEventHostCommandKind::FollowCompletion,
            FollowId,
            "Nine Follow completion"
        );
        const FTSShareProcessingDispatch& ShareDispatch =
            RequireShareDispatch(FollowCompletion, "Core selects Share");
        Require(
            ShareDispatch.Emission.EmissionId == ShareId &&
                std::get_if<FTSShareMilestoneProcessingDispatch>(
                    &*FollowCompletion.Dispatch
                ) == nullptr,
            "Share priority and distinct dispatch"
        );
        RequireShareInputEqual(
            ShareDispatch.Payload.Input,
            ShareInput,
            "Nine Share payload"
        );

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Nine Share completion must signal"
        );
        const FTSEventHostCycleResult ShareCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ShareCompletion,
            ETSEventHostCommandKind::ShareCompletion,
            ShareId,
            "Nine Share completion"
        );
        const FTSShareMilestoneProcessingDispatch& MilestoneDispatch =
            RequireShareMilestoneDispatch(
                ShareCompletion,
                "Core selects ShareMilestone"
            );
        Require(
            MilestoneDispatch.Emission.EmissionId == MilestoneId &&
                std::get_if<FTSShareProcessingDispatch>(
                    &*ShareCompletion.Dispatch
                ) == nullptr,
            "ShareMilestone priority and distinct dispatch"
        );
        RequireShareInputEqual(
            MilestoneDispatch.Payload.Input,
            MilestoneInput,
            "Nine ShareMilestone payload"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                MilestoneId,
                ETSProcessingResult::Succeeded
            ),
            "Nine ShareMilestone completion must signal"
        );
        const FTSEventHostCycleResult MilestoneCompletion =
            Host.RunOneCycle();
        RequireSuccessfulCompletion(
            MilestoneCompletion,
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            MilestoneId,
            "Nine ShareMilestone completion"
        );
        const FTSRoomUserProcessingDispatch& RoomUserDispatch =
            RequireRoomUserDispatch(
                MilestoneCompletion,
                "Core selects RoomUser"
            );
        Require(
            RoomUserDispatch.Emission.EmissionId == RoomUserId,
            "RoomUser priority"
        );
        RequireRoomUserInputEqual(
            RoomUserDispatch.Payload.Input,
            RoomUserInput,
            "Nine RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "Nine RoomUser completion must signal"
        );
        const FTSEventHostCycleResult RoomUserCompletion =
            Host.RunOneCycle();
        RequireSuccessfulCompletion(
            RoomUserCompletion,
            ETSEventHostCommandKind::RoomUserCompletion,
            RoomUserId,
            "Nine RoomUser completion"
        );
        const FTSLikeProcessingDispatch& LikeDispatch =
            RequireLikeDispatch(RoomUserCompletion, "Core selects Like");
        Require(LikeDispatch.Emission.EmissionId == LikeId, "Like priority");
        RequireLikeInputEqual(
            LikeDispatch.Payload.Input,
            LikeInput,
            "Nine Like payload"
        );

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Nine Like completion must signal"
        );
        const FTSEventHostCycleResult LikeCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            LikeCompletion,
            ETSEventHostCommandKind::LikeCompletion,
            LikeId,
            "Nine Like completion"
        );
        const FTSMemberProcessingDispatch& MemberDispatch =
            RequireMemberDispatch(LikeCompletion, "Core selects Member");
        Require(
            MemberDispatch.Emission.EmissionId == MemberId,
            "Member must be the final dispatch"
        );
        RequireMemberInputEqual(
            MemberDispatch.Payload.Input,
            MemberInput,
            "Nine Member payload"
        );

        Require(
            Host.PostMemberCompletion(
                MemberId,
                ETSProcessingResult::Succeeded
            ),
            "Nine Member completion must signal"
        );
        const FTSEventHostCycleResult MemberCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            MemberCompletion,
            ETSEventHostCommandKind::MemberCompletion,
            MemberId,
            "Nine Member completion"
        );
        Require(
            !MemberCompletion.Dispatch.has_value() &&
                !MemberCompletion.bMoreCommandsPending &&
                MemberCompletion.NextWakeTime.Status ==
                    ETSNextWakeStatus::NoWakeScheduled,
            "Nine-route scenario must leave no work"
        );
    }

    void TestShareMilestoneCompletionCapturesReadyShare()
    {
        FTSEventExecutionHost Host(MakeShareAndShareMilestoneSettings());
        const FTSShareInput MilestoneInput =
            MakeShareInput("milestone-before-share");
        const FTSShareInput ShareInput =
            MakeShareInput("share-after-milestone");

        Require(
            Host.PostShareMilestone(MilestoneInput),
            "Processing ShareMilestone must signal"
        );
        const FTSEventHostCycleResult MilestoneCycle = Host.RunOneCycle();
        const FTSEmissionId MilestoneId =
            RequireAcceptedShareMilestoneAdmission(
                MilestoneCycle,
                "Processing ShareMilestone"
            );
        (void)RequireShareMilestoneDispatch(
            MilestoneCycle,
            "Processing ShareMilestone"
        );

        Require(Host.PostShare(ShareInput), "Pending Share must signal");
        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Pending Share");
        RequireBusyPendingCycle(
            ShareCycle,
            false,
            "Share waits behind ShareMilestone"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                MilestoneId,
                ETSProcessingResult::Succeeded
            ),
            "ShareMilestone completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            MilestoneId,
            "ShareMilestone completion captures Share"
        );
        const FTSShareProcessingDispatch& Dispatch =
            RequireShareDispatch(
                CompletionCycle,
                "Share captured after ShareMilestone"
            );
        Require(Dispatch.Emission.EmissionId == ShareId, "Captured Share ID");
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            ShareInput,
            "Captured Share payload"
        );

        Require(
            Host.PostShareCompletion(
                ShareId,
                ETSProcessingResult::Succeeded
            ),
            "Captured Share cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ShareCompletion,
            ShareId,
            "Captured Share cleanup"
        );
    }

    void TestShareCompletionCapturesReadyShareMilestone()
    {
        FTSEventExecutionHost Host(MakeShareAndShareMilestoneSettings());
        const FTSShareInput ShareInput =
            MakeShareInput("share-before-milestone");
        const FTSShareInput MilestoneInput =
            MakeShareInput("milestone-after-share");

        Require(Host.PostShare(ShareInput), "Processing Share must signal");
        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Processing Share");
        (void)RequireShareDispatch(ShareCycle, "Processing Share");

        Require(
            Host.PostShareMilestone(MilestoneInput),
            "Pending ShareMilestone must signal"
        );
        const FTSEventHostCycleResult MilestoneCycle = Host.RunOneCycle();
        const FTSEmissionId MilestoneId =
            RequireAcceptedShareMilestoneAdmission(
                MilestoneCycle,
                "Pending ShareMilestone"
            );
        RequireBusyPendingCycle(
            MilestoneCycle,
            false,
            "ShareMilestone waits behind Share"
        );

        Require(
            Host.PostShareCompletion(
                ShareId,
                ETSProcessingResult::Succeeded
            ),
            "Share completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ShareCompletion,
            ShareId,
            "Share completion captures ShareMilestone"
        );
        const FTSShareMilestoneProcessingDispatch& Dispatch =
            RequireShareMilestoneDispatch(
                CompletionCycle,
                "ShareMilestone captured after Share"
            );
        Require(
            Dispatch.Emission.EmissionId == MilestoneId,
            "Captured ShareMilestone ID"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            MilestoneInput,
            "Captured ShareMilestone payload"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                MilestoneId,
                ETSProcessingResult::Succeeded
            ),
            "Captured ShareMilestone cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            MilestoneId,
            "Captured ShareMilestone cleanup"
        );
    }

    void TestShareMilestoneCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host(MakeShareMilestoneSettings(10));
        const FTSShareInput FirstInput =
            MakeShareInput("milestone-cancel-first");
        const FTSShareInput SecondInput =
            MakeShareInput("milestone-cancel-second");

        Require(
            Host.PostShareMilestone(FirstInput),
            "First ShareMilestone must signal"
        );
        Require(
            !Host.PostShareMilestone(SecondInput),
            "Second ShareMilestone must queue"
        );

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedShareMilestoneAdmission(
                FirstCycle,
                "First cancel ShareMilestone"
            );
        (void)RequireShareMilestoneDispatch(
            FirstCycle,
            "First cancel ShareMilestone"
        );

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedShareMilestoneAdmission(
                SecondCycle,
                "Second cancel ShareMilestone"
            );
        RequireBusyPendingCycle(
            SecondCycle,
            false,
            "Second ShareMilestone pending"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                FirstId,
                ETSProcessingResult::Cancelled
            ),
            "First ShareMilestone cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::ShareMilestoneCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First ShareMilestone cancellation"
            ),
            FirstId,
            "First ShareMilestone cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "ShareMilestone cancel must advance through explicit Host Pump"
        );
        RequireShareInputEqual(
            RequireShareMilestoneDispatch(
                CancelCycle,
                "Second ShareMilestone after cancel"
            ).Payload.Input,
            SecondInput,
            "Second ShareMilestone after cancel payload"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                SecondId,
                ETSProcessingResult::Succeeded
            ),
            "Second ShareMilestone completion must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            SecondId,
            "Second ShareMilestone completion"
        );
    }

    void TestWrongRouteShareAndShareMilestoneCompletionsFailBeforeCoreMutation()
    {
        {
            FTSEventExecutionHost Host;
            Require(
                Host.PostShareMilestone(
                    MakeShareInput("wrong-share-completion")
                ),
                "Wrong-route ShareMilestone must signal"
            );
            const FTSEventHostCycleResult AdmissionCycle =
                Host.RunOneCycle();
            const FTSEmissionId MilestoneId =
                RequireAcceptedShareMilestoneAdmission(
                    AdmissionCycle,
                    "Wrong-route ShareMilestone"
                );
            (void)RequireShareMilestoneDispatch(
                AdmissionCycle,
                "Wrong-route ShareMilestone"
            );

            Require(
                Host.PostShareCompletion(
                    MilestoneId,
                    ETSProcessingResult::Succeeded
                ),
                "Wrong Share completion must signal"
            );
            bool bRejected = false;
            try
            {
                (void)Host.RunOneCycle();
            }
            catch (const FTSRejectedProcessingCompletionError&)
            {
                bRejected = true;
            }
            Require(
                bRejected,
                "Share completion must reject ShareMilestone before mutation"
            );

            Require(
                Host.PostShareMilestoneCompletion(
                    MilestoneId,
                    ETSProcessingResult::Succeeded
                ),
                "Correct ShareMilestone completion must signal"
            );
            RequireSuccessfulCompletion(
                Host.RunOneCycle(),
                ETSEventHostCommandKind::ShareMilestoneCompletion,
                MilestoneId,
                "Correct ShareMilestone completion"
            );
        }

        {
            FTSEventExecutionHost Host;
            Require(
                Host.PostShare(MakeShareInput("wrong-milestone-completion")),
                "Wrong-route Share must signal"
            );
            const FTSEventHostCycleResult AdmissionCycle =
                Host.RunOneCycle();
            const FTSEmissionId ShareId =
                RequireAcceptedShareAdmission(
                    AdmissionCycle,
                    "Wrong-route Share"
                );
            (void)RequireShareDispatch(AdmissionCycle, "Wrong-route Share");

            Require(
                Host.PostShareMilestoneCompletion(
                    ShareId,
                    ETSProcessingResult::Succeeded
                ),
                "Wrong ShareMilestone completion must signal"
            );
            bool bRejected = false;
            try
            {
                (void)Host.RunOneCycle();
            }
            catch (const FTSRejectedProcessingCompletionError&)
            {
                bRejected = true;
            }
            Require(
                bRejected,
                "ShareMilestone completion must reject Share before mutation"
            );

            Require(
                Host.PostShareCompletion(
                    ShareId,
                    ETSProcessingResult::Succeeded
                ),
                "Correct Share completion must signal"
            );
            RequireSuccessfulCompletion(
                Host.RunOneCycle(),
                ETSEventHostCommandKind::ShareCompletion,
                ShareId,
                "Correct Share completion"
            );
        }
    }

    void TestPendingShareMilestoneExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatShareMilestoneSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("milestone-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(
                ChatCycle,
                "ShareMilestone expiry Chat"
            );
        (void)RequireChatDispatch(
            ChatCycle,
            "ShareMilestone expiry Chat"
        );

        Require(
            Host.PostShareMilestone(
                MakeShareInput("expiring-share-milestone")
            ),
            "Expiring ShareMilestone publication must signal"
        );
        const FTSEventHostCycleResult MilestoneCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringId =
            RequireAcceptedShareMilestoneAdmission(
                MilestoneCycle,
                "Expiring ShareMilestone"
            );
        RequireBusyPendingCycle(
            MilestoneCycle,
            false,
            "ShareMilestone pending"
        );

        Clock.Advance(6s);
        const FTSShareInput ReplacementInput =
            MakeShareInput("share-milestone-after-expiry");
        Require(
            Host.PostShareMilestone(ReplacementInput),
            "Replacement ShareMilestone must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle =
            Host.RunOneCycle();
        const FTSEmissionId ReplacementId =
            RequireAcceptedShareMilestoneAdmission(
                ReplacementCycle,
                "ShareMilestone after expiration"
            );
        const FTSEmissionLifecycleEvents& Lifecycle =
            ReplacementCycle.DueExpirations.LifecycleEvents;
        Require(
            Lifecycle.size() == 1 &&
                Lifecycle.front().Envelope.EmissionId == ExpiringId &&
                Lifecycle.front().Envelope.Flow ==
                    ETSEventFlow::ShareMilestone &&
                Lifecycle.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Replacement cycle must report expired ShareMilestone"
        );
        RequireBusyPendingCycle(
            ReplacementCycle,
            false,
            "Replacement waits while Chat processes"
        );

        Require(
            Host.PostChatCompletion(
                ChatId,
                ETSProcessingResult::Succeeded
            ),
            "ShareMilestone expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle =
            Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "ShareMilestone expiry Chat completion"
        );
        const FTSShareMilestoneProcessingDispatch& Dispatch =
            RequireShareMilestoneDispatch(
                CompletionCycle,
                "Replacement ShareMilestone dispatch"
            );
        Require(
            Dispatch.Emission.EmissionId == ReplacementId,
            "Replacement ShareMilestone identity"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            ReplacementInput,
            "Replacement ShareMilestone payload"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement ShareMilestone cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            ReplacementId,
            "Replacement ShareMilestone cleanup"
        );
    }

    void TestShareMilestoneFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeShareInput("failed-share-milestone"),
            MakeShareInput("after-failed-share-milestone"),
            ETSEventHostCommandKind::ShareMilestoneCompletion,
            [](FTSEventExecutionHost& Host, FTSShareInput Input)
            {
                return Host.PostShareMilestone(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostShareMilestoneCompletion(
                    EmissionId,
                    Result
                );
            },
            [](const FTSEventHostCycleResult& Cycle,
               const std::string& Context)
            {
                return RequireAcceptedShareMilestoneAdmission(
                    Cycle,
                    Context
                );
            },
            [](const FTSEventHostCycleResult& Cycle,
               const std::string& Context)
            {
                return RequireShareMilestoneDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "ShareMilestone Failed"
        );
    }

    void TestExpirationsBeforeShareMilestoneCompletionRemainInDueExpirations()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings =
            MakeShareMilestoneSettings(10, 5s);
        FTSEventExecutionHost Host(Settings, Clock.MakeProvider());

        Require(
            Host.PostChat(MakeChatInput("milestone-partition-chat")),
            "Partition Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Partition Chat");
        (void)RequireChatDispatch(ChatCycle, "Partition Chat");

        Require(
            Host.PostShareMilestone(
                MakeShareInput("partition-expiring-milestone")
            ),
            "Partition ShareMilestone must signal"
        );
        const FTSEventHostCycleResult MilestoneCycle = Host.RunOneCycle();
        const FTSEmissionId MilestoneId =
            RequireAcceptedShareMilestoneAdmission(
                MilestoneCycle,
                "Partition ShareMilestone"
            );
        RequireBusyPendingCycle(
            MilestoneCycle,
            false,
            "Partition ShareMilestone pending"
        );

        Require(
            Host.PostFollow(MakeFollowInput("partition-follow")),
            "Partition Follow must signal"
        );
        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Partition Follow");
        RequireBusyPendingCycle(
            FollowCycle,
            false,
            "Partition Follow pending"
        );

        Clock.Advance(6s);
        Require(
            Host.PostChatCompletion(
                ChatId,
                ETSProcessingResult::Succeeded
            ),
            "Partition Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle =
            Host.RunOneCycle();
        Require(
            CompletionCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Envelope.EmissionId == MilestoneId &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Envelope.Flow == ETSEventFlow::ShareMilestone &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Expired ShareMilestone must remain in DueExpirations"
        );

        const FTSProcessingCompletionResult& Completion =
            RequireCompletion(
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
            "ShareMilestone expiration must not duplicate in Confirm lifecycle"
        );
        Require(
            Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == FollowId,
            "Confirm Auto Pump must select surviving Follow"
        );
        Require(
            RequireFollowDispatch(
                CompletionCycle,
                "Partition Follow dispatch"
            ).Emission.EmissionId == FollowId,
            "Surviving Follow must dispatch in completion cycle"
        );

        Require(
            Host.PostFollowCompletion(
                FollowId,
                ETSProcessingResult::Succeeded
            ),
            "Partition Follow cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::FollowCompletion,
            FollowId,
            "Partition Follow cleanup"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareMilestoneHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "ShareMilestone input Auto Pumps and dispatches",
            &TestShareMilestoneInputAutoPumpsAndDispatches
        });
        Tests.push_back({
            "Worker PostShareMilestone runs on owner",
            &TestWorkerPostShareMilestoneRunsOnOwner
        });
        Tests.push_back({
            "Nine routes preserve Host FIFO and Core order",
            &TestNineRoutesPreserveHostFifoAndCoreOrder
        });
        Tests.push_back({
            "ShareMilestone completion captures ready Share",
            &TestShareMilestoneCompletionCapturesReadyShare
        });
        Tests.push_back({
            "Share completion captures ready ShareMilestone",
            &TestShareCompletionCapturesReadyShareMilestone
        });
        Tests.push_back({
            "ShareMilestone cancel advances with explicit Pump",
            &TestShareMilestoneCancelAdvancesWithExplicitPump
        });
        Tests.push_back({
            "Wrong-route Share and ShareMilestone completions fail before Core mutation",
            &TestWrongRouteShareAndShareMilestoneCompletionsFailBeforeCoreMutation
        });
        Tests.push_back({
            "Pending ShareMilestone expires while Chat is Processing",
            &TestPendingShareMilestoneExpiresWhileChatIsProcessing
        });
        Tests.push_back({
            "ShareMilestone Failed completion is terminal and Host recovers",
            &TestShareMilestoneFailedCompletionIsTerminalAndHostRecovers
        });
        Tests.push_back({
            "Expirations before ShareMilestone completion remain in DueExpirations",
            &TestExpirationsBeforeShareMilestoneCompletionRemainInDueExpirations
        });
    }
}
