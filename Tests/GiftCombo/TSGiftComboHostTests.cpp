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

    void MutateGiftInput(FTSGiftInput& Input)
    {
        Input.GiftId = -1;
        Input.GiftName = "mutated";
        Input.GiftPictureUrl = "mutated";
        Input.DiamondCount = -1;
        Input.RepeatCount = -1;
        Input.GiftType = -1;
        Input.Describe = "mutated";
        Input.bRepeatEnd = false;
        Input.GroupId = "mutated";
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

    void TestGiftComboInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSGiftInput Input = MakeGiftInput("gift-combo-auto");
        const FTSGiftInput Expected = Input;

        Require(
            Host.PostGiftCombo(Input),
            "First GiftCombo publication must signal"
        );
        MutateGiftInput(Input);

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedGiftComboAdmission(Cycle, "GiftCombo Auto Pump");
        const FTSGiftComboProcessingDispatch& Dispatch =
            RequireGiftComboDispatch(Cycle, "GiftCombo Auto Pump");
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::GiftCombo,
            "GiftCombo Auto Pump identity and flow"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "GiftCombo Auto Pump snapshot"
        );
        Require(
            std::get_if<FTSGiftProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSShareProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSLikeProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSRoomUserProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                std::get_if<FTSMemberProcessingDispatch>(&*Cycle.Dispatch) ==
                    nullptr &&
                !Cycle.bMoreCommandsPending,
            "GiftCombo dispatch must contain no other route"
        );

        Require(
            Host.PostGiftComboCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "GiftCombo completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::GiftComboCompletion,
            EmissionId,
            "GiftCombo Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "GiftCombo completion must clean all work"
        );
    }

    void TestWorkerPostGiftComboRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(FTSEventQueueSettings{}, NowProvider);
        FTSGiftInput Input = MakeGiftInput("gift-combo-worker");
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
                    bScheduleRequested = Host.PostGiftCombo(Input);
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
            "Worker PostGiftCombo must only request owner scheduling"
        );

        MutateGiftInput(Input);
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedGiftComboAdmission(Cycle, "Worker GiftCombo");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the GiftCombo pipeline"
        );
        RequireGiftInputEqual(
            RequireGiftComboDispatch(Cycle, "Worker GiftCombo").Payload.Input,
            Expected,
            "Worker GiftCombo snapshot"
        );

        Require(
            Host.PostGiftComboCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker GiftCombo cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::GiftComboCompletion,
            EmissionId,
            "Worker GiftCombo cleanup"
        );
    }

    void TestEightRoutesPreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("eight-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("eight-follow");
        const FTSShareInput ShareInput = MakeShareInput("eight-share");
        const FTSLikeInput LikeInput = MakeLikeInput("eight-like");
        const FTSRoomUserInput RoomUserInput = MakeRoomUserInput("eight-room");
        const FTSGiftInput GiftInput = MakeGiftInput("eight-gift");
        const FTSGiftInput GiftComboInput = MakeGiftInput("eight-combo");
        const FTSMemberInput MemberInput = MakeMemberInput("eight-member");

        Require(Host.PostChat(ChatInput), "Eight Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Eight Follow must queue");
        Require(!Host.PostShare(ShareInput), "Eight Share must queue");
        Require(!Host.PostLike(LikeInput), "Eight Like must queue");
        Require(!Host.PostRoomUser(RoomUserInput), "Eight RoomUser must queue");
        Require(!Host.PostGift(GiftInput), "Eight Gift must queue");
        Require(
            !Host.PostGiftCombo(GiftComboInput),
            "Eight GiftCombo must queue"
        );
        Require(!Host.PostMember(MemberInput), "Eight Member must queue");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Eight FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Eight FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Eight-route FIFO must process Chat first"
        );
        RequireChatPayloadMatchesInput(
            RequireChatDispatch(ChatCycle, "Eight FIFO Chat").Payload,
            ChatInput,
            "Eight FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Eight FIFO Follow");
        RequireBusyPendingCycle(FollowCycle, true, "Follow admitted second");

        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Eight FIFO Share");
        RequireBusyPendingCycle(ShareCycle, true, "Share admitted third");

        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Eight FIFO Like");
        RequireBusyPendingCycle(LikeCycle, true, "Like admitted fourth");

        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Eight FIFO RoomUser"
        );
        RequireBusyPendingCycle(RoomUserCycle, true, "RoomUser admitted fifth");

        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Eight FIFO Gift");
        RequireBusyPendingCycle(GiftCycle, true, "Gift admitted sixth");

        const FTSEventHostCycleResult GiftComboCycle = Host.RunOneCycle();
        const FTSEmissionId GiftComboId =
            RequireAcceptedGiftComboAdmission(
                GiftComboCycle,
                "Eight FIFO GiftCombo"
            );
        RequireBusyPendingCycle(
            GiftComboCycle,
            true,
            "GiftCombo admitted seventh"
        );

        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(MemberCycle, "Eight FIFO Member");
        RequireBusyPendingCycle(MemberCycle, false, "Member admitted eighth");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Eight Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ChatCompletion,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Eight Chat completion"
        );
        const FTSGiftComboProcessingDispatch& ComboDispatch =
            RequireGiftComboDispatch(
                ChatCompletion,
                "Core selects GiftCombo"
            );
        Require(
            ComboDispatch.Emission.EmissionId == GiftComboId &&
                std::get_if<FTSGiftProcessingDispatch>(
                    &*ChatCompletion.Dispatch
                ) == nullptr,
            "GiftCombo priority must preserve its distinct dispatch type"
        );
        RequireGiftInputEqual(
            ComboDispatch.Payload.Input,
            GiftComboInput,
            "Eight GiftCombo payload"
        );

        Require(
            Host.PostGiftComboCompletion(
                GiftComboId,
                ETSProcessingResult::Succeeded
            ),
            "Eight GiftCombo completion must signal"
        );
        const FTSEventHostCycleResult ComboCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ComboCompletion,
            ETSEventHostCommandKind::GiftComboCompletion,
            GiftComboId,
            "Eight GiftCombo completion"
        );
        const FTSGiftProcessingDispatch& GiftDispatch =
            RequireGiftDispatch(ComboCompletion, "Core selects Gift");
        Require(GiftDispatch.Emission.EmissionId == GiftId, "Gift priority");
        RequireGiftInputEqual(
            GiftDispatch.Payload.Input,
            GiftInput,
            "Eight Gift payload"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Eight Gift completion must signal"
        );
        const FTSEventHostCycleResult GiftCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            GiftCompletion,
            ETSEventHostCommandKind::GiftCompletion,
            GiftId,
            "Eight Gift completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(GiftCompletion, "Core selects Follow");
        Require(FollowDispatch.Emission.EmissionId == FollowId, "Follow priority");
        RequireFollowInputEqual(
            FollowDispatch.Payload.Input,
            FollowInput,
            "Eight Follow payload"
        );

        Require(
            Host.PostFollowCompletion(FollowId, ETSProcessingResult::Succeeded),
            "Eight Follow completion must signal"
        );
        const FTSEventHostCycleResult FollowCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            FollowCompletion,
            ETSEventHostCommandKind::FollowCompletion,
            FollowId,
            "Eight Follow completion"
        );
        const FTSShareProcessingDispatch& ShareDispatch =
            RequireShareDispatch(FollowCompletion, "Core selects Share");
        Require(ShareDispatch.Emission.EmissionId == ShareId, "Share priority");
        RequireShareInputEqual(
            ShareDispatch.Payload.Input,
            ShareInput,
            "Eight Share payload"
        );

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Eight Share completion must signal"
        );
        const FTSEventHostCycleResult ShareCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            ShareCompletion,
            ETSEventHostCommandKind::ShareCompletion,
            ShareId,
            "Eight Share completion"
        );
        const FTSRoomUserProcessingDispatch& RoomUserDispatch =
            RequireRoomUserDispatch(
                ShareCompletion,
                "Core selects RoomUser"
            );
        Require(
            RoomUserDispatch.Emission.EmissionId == RoomUserId,
            "RoomUser priority"
        );
        RequireRoomUserInputEqual(
            RoomUserDispatch.Payload.Input,
            RoomUserInput,
            "Eight RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "Eight RoomUser completion must signal"
        );
        const FTSEventHostCycleResult RoomUserCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            RoomUserCompletion,
            ETSEventHostCommandKind::RoomUserCompletion,
            RoomUserId,
            "Eight RoomUser completion"
        );
        const FTSLikeProcessingDispatch& LikeDispatch =
            RequireLikeDispatch(RoomUserCompletion, "Core selects Like");
        Require(LikeDispatch.Emission.EmissionId == LikeId, "Like priority");
        RequireLikeInputEqual(
            LikeDispatch.Payload.Input,
            LikeInput,
            "Eight Like payload"
        );

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Eight Like completion must signal"
        );
        const FTSEventHostCycleResult LikeCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            LikeCompletion,
            ETSEventHostCommandKind::LikeCompletion,
            LikeId,
            "Eight Like completion"
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
            "Eight Member payload"
        );

        Require(
            Host.PostMemberCompletion(MemberId, ETSProcessingResult::Succeeded),
            "Eight Member completion must signal"
        );
        const FTSEventHostCycleResult MemberCompletion = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            MemberCompletion,
            ETSEventHostCommandKind::MemberCompletion,
            MemberId,
            "Eight Member completion"
        );
        Require(
            !MemberCompletion.Dispatch.has_value() &&
                !MemberCompletion.bMoreCommandsPending,
            "Eight-route scenario must leave no work"
        );
    }

    void TestGiftComboCompletionCapturesReadyGift()
    {
        FTSEventExecutionHost Host(MakeGiftAndGiftComboSettings());
        const FTSGiftInput ComboInput = MakeGiftInput("combo-before-gift");
        const FTSGiftInput GiftInput = MakeGiftInput("gift-after-combo");

        Require(Host.PostGiftCombo(ComboInput), "Processing GiftCombo must signal");
        const FTSEventHostCycleResult ComboCycle = Host.RunOneCycle();
        const FTSEmissionId ComboId =
            RequireAcceptedGiftComboAdmission(ComboCycle, "Processing GiftCombo");
        RequireGiftInputEqual(
            RequireGiftComboDispatch(ComboCycle, "Processing GiftCombo")
                .Payload.Input,
            ComboInput,
            "Processing GiftCombo payload"
        );

        Require(Host.PostGift(GiftInput), "Pending Gift must signal");
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Pending Gift");
        RequireBusyPendingCycle(GiftCycle, false, "Gift waits behind GiftCombo");

        Require(
            Host.PostGiftComboCompletion(
                ComboId,
                ETSProcessingResult::Succeeded
            ),
            "GiftCombo completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::GiftComboCompletion,
            ComboId,
            "GiftCombo completion captures Gift"
        );
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(CompletionCycle, "Gift captured after GiftCombo");
        Require(Dispatch.Emission.EmissionId == GiftId, "Captured Gift ID");
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            GiftInput,
            "Captured Gift payload"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Captured Gift cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::GiftCompletion,
            GiftId,
            "Captured Gift cleanup"
        );
    }

    void TestGiftCompletionCapturesReadyGiftCombo()
    {
        FTSEventExecutionHost Host(MakeGiftAndGiftComboSettings());
        const FTSGiftInput GiftInput = MakeGiftInput("gift-before-combo");
        const FTSGiftInput ComboInput = MakeGiftInput("combo-after-gift");

        Require(Host.PostGift(GiftInput), "Processing Gift must signal");
        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Processing Gift");
        (void)RequireGiftDispatch(GiftCycle, "Processing Gift");

        Require(Host.PostGiftCombo(ComboInput), "Pending GiftCombo must signal");
        const FTSEventHostCycleResult ComboCycle = Host.RunOneCycle();
        const FTSEmissionId ComboId =
            RequireAcceptedGiftComboAdmission(ComboCycle, "Pending GiftCombo");
        RequireBusyPendingCycle(
            ComboCycle,
            false,
            "GiftCombo waits behind Gift"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Gift completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::GiftCompletion,
            GiftId,
            "Gift completion captures GiftCombo"
        );
        const FTSGiftComboProcessingDispatch& Dispatch =
            RequireGiftComboDispatch(
                CompletionCycle,
                "GiftCombo captured after Gift"
            );
        Require(Dispatch.Emission.EmissionId == ComboId, "Captured GiftCombo ID");
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            ComboInput,
            "Captured GiftCombo payload"
        );

        Require(
            Host.PostGiftComboCompletion(
                ComboId,
                ETSProcessingResult::Succeeded
            ),
            "Captured GiftCombo cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::GiftComboCompletion,
            ComboId,
            "Captured GiftCombo cleanup"
        );
    }

    void TestGiftComboCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host(MakeGiftComboSettings(10));
        const FTSGiftInput FirstInput = MakeGiftInput("combo-cancel-first");
        const FTSGiftInput SecondInput = MakeGiftInput("combo-cancel-second");

        Require(Host.PostGiftCombo(FirstInput), "First GiftCombo must signal");
        Require(!Host.PostGiftCombo(SecondInput), "Second GiftCombo must queue");

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedGiftComboAdmission(FirstCycle, "First cancel combo");
        (void)RequireGiftComboDispatch(FirstCycle, "First cancel combo");

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedGiftComboAdmission(SecondCycle, "Second cancel combo");
        RequireBusyPendingCycle(SecondCycle, false, "Second combo pending");

        Require(
            Host.PostGiftComboCompletion(
                FirstId,
                ETSProcessingResult::Cancelled
            ),
            "First GiftCombo cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::GiftComboCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First GiftCombo cancellation"
            ),
            FirstId,
            "First GiftCombo cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "GiftCombo cancel must advance through explicit Host Pump"
        );
        const FTSGiftComboProcessingDispatch& SecondDispatch =
            RequireGiftComboDispatch(CancelCycle, "Second combo after cancel");
        RequireGiftInputEqual(
            SecondDispatch.Payload.Input,
            SecondInput,
            "Second combo after cancel payload"
        );

        Require(
            Host.PostGiftComboCompletion(
                SecondId,
                ETSProcessingResult::Succeeded
            ),
            "Second GiftCombo completion must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::GiftComboCompletion,
            SecondId,
            "Second GiftCombo completion"
        );
    }

    void TestWrongRouteGiftAndGiftComboCompletionsFailBeforeCoreMutation()
    {
        {
            FTSEventExecutionHost Host;
            Require(
                Host.PostGiftCombo(MakeGiftInput("wrong-gift-completion")),
                "Wrong-route GiftCombo must signal"
            );
            const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
            const FTSEmissionId ComboId = RequireAcceptedGiftComboAdmission(
                AdmissionCycle,
                "Wrong-route GiftCombo"
            );
            (void)RequireGiftComboDispatch(
                AdmissionCycle,
                "Wrong-route GiftCombo"
            );

            Require(
                Host.PostGiftCompletion(
                    ComboId,
                    ETSProcessingResult::Succeeded
                ),
                "Wrong Gift completion must signal"
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
                "Gift completion must reject GiftCombo before Core mutation"
            );

            Require(
                Host.PostGiftComboCompletion(
                    ComboId,
                    ETSProcessingResult::Succeeded
                ),
                "Correct GiftCombo completion must signal"
            );
            RequireSuccessfulCompletion(
                Host.RunOneCycle(),
                ETSEventHostCommandKind::GiftComboCompletion,
                ComboId,
                "Correct GiftCombo completion"
            );
        }

        {
            FTSEventExecutionHost Host;
            Require(
                Host.PostGift(MakeGiftInput("wrong-combo-completion")),
                "Wrong-route Gift must signal"
            );
            const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
            const FTSEmissionId GiftId =
                RequireAcceptedGiftAdmission(AdmissionCycle, "Wrong-route Gift");
            (void)RequireGiftDispatch(AdmissionCycle, "Wrong-route Gift");

            Require(
                Host.PostGiftComboCompletion(
                    GiftId,
                    ETSProcessingResult::Succeeded
                ),
                "Wrong GiftCombo completion must signal"
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
                "GiftCombo completion must reject Gift before Core mutation"
            );

            Require(
                Host.PostGiftCompletion(
                    GiftId,
                    ETSProcessingResult::Succeeded
                ),
                "Correct Gift completion must signal"
            );
            RequireSuccessfulCompletion(
                Host.RunOneCycle(),
                ETSEventHostCommandKind::GiftCompletion,
                GiftId,
                "Correct Gift completion"
            );
        }
    }

    void TestPendingGiftComboExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatGiftComboSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("combo-expiry-chat")),
            "Expiry Chat publication must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "GiftCombo expiry Chat");
        (void)RequireChatDispatch(ChatCycle, "GiftCombo expiry Chat");

        Require(
            Host.PostGiftCombo(MakeGiftInput("expiring-combo")),
            "Expiring GiftCombo publication must signal"
        );
        const FTSEventHostCycleResult ComboCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringId =
            RequireAcceptedGiftComboAdmission(ComboCycle, "Expiring GiftCombo");
        RequireBusyPendingCycle(ComboCycle, false, "GiftCombo pending");

        Clock.Advance(6s);
        const FTSGiftInput ReplacementInput =
            MakeGiftInput("combo-after-expiry");
        Require(
            Host.PostGiftCombo(ReplacementInput),
            "Replacement GiftCombo must signal"
        );
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId =
            RequireAcceptedGiftComboAdmission(
                ReplacementCycle,
                "GiftCombo after expiration"
            );
        const FTSEmissionLifecycleEvents& Lifecycle =
            ReplacementCycle.DueExpirations.LifecycleEvents;
        Require(
            Lifecycle.size() == 1 &&
                Lifecycle.front().Envelope.EmissionId == ExpiringId &&
                Lifecycle.front().Envelope.Flow == ETSEventFlow::GiftCombo &&
                Lifecycle.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Replacement cycle must report expired GiftCombo"
        );
        RequireBusyPendingCycle(
            ReplacementCycle,
            false,
            "Replacement waits while Chat processes"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "GiftCombo expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "GiftCombo expiry Chat completion"
        );
        const FTSGiftComboProcessingDispatch& Dispatch =
            RequireGiftComboDispatch(
                CompletionCycle,
                "Replacement GiftCombo dispatch"
            );
        Require(
            Dispatch.Emission.EmissionId == ReplacementId,
            "Replacement GiftCombo identity"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            ReplacementInput,
            "Replacement GiftCombo payload"
        );

        Require(
            Host.PostGiftComboCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement GiftCombo cleanup must signal"
        );
        RequireSuccessfulCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::GiftComboCompletion,
            ReplacementId,
            "Replacement GiftCombo cleanup"
        );
    }

    void TestGiftComboFailedCompletionIsTerminalAndHostRecovers()
    {
        RunFailedCompletionHostScenario(
            MakeGiftInput("failed-combo"),
            MakeGiftInput("after-failed-combo"),
            ETSEventHostCommandKind::GiftComboCompletion,
            [](FTSEventExecutionHost& Host, FTSGiftInput Input)
            {
                return Host.PostGiftCombo(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostGiftComboCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedGiftComboAdmission(Cycle, Context);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireGiftComboDispatch(Cycle, Context)
                    .Emission.EmissionId;
            },
            "GiftCombo Failed"
        );
    }

    void TestExpirationsBeforeGiftComboCompletionRemainInDueExpirations()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeGiftComboSettings(10, 5s);
        FTSEventExecutionHost Host(Settings, Clock.MakeProvider());

        Require(
            Host.PostChat(MakeChatInput("combo-partition-chat")),
            "Partition Chat must signal"
        );
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Partition Chat");
        (void)RequireChatDispatch(ChatCycle, "Partition Chat");

        Require(
            Host.PostGiftCombo(MakeGiftInput("partition-expiring-combo")),
            "Partition GiftCombo must signal"
        );
        const FTSEventHostCycleResult ComboCycle = Host.RunOneCycle();
        const FTSEmissionId ComboId =
            RequireAcceptedGiftComboAdmission(
                ComboCycle,
                "Partition GiftCombo"
            );
        RequireBusyPendingCycle(ComboCycle, false, "Partition combo pending");

        Require(
            Host.PostFollow(MakeFollowInput("partition-follow")),
            "Partition Follow must signal"
        );
        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Partition Follow");
        RequireBusyPendingCycle(FollowCycle, false, "Partition Follow pending");

        Clock.Advance(6s);
        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Partition Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        Require(
            CompletionCycle.DueExpirations.LifecycleEvents.size() == 1 &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Envelope.EmissionId == ComboId &&
                CompletionCycle.DueExpirations.LifecycleEvents.front()
                        .Envelope.Flow == ETSEventFlow::GiftCombo &&
                CompletionCycle.DueExpirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Expired GiftCombo must remain in DueExpirations"
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
            "GiftCombo expiration must not duplicate in Confirm lifecycle"
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
    void RegisterGiftComboHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "GiftCombo input Auto Pumps and dispatches",
            &TestGiftComboInputAutoPumpsAndDispatches
        });
        Tests.push_back({
            "Worker PostGiftCombo runs on owner",
            &TestWorkerPostGiftComboRunsOnOwner
        });
        Tests.push_back({
            "Eight routes preserve Host FIFO and Core order",
            &TestEightRoutesPreserveHostFifoAndCoreOrder
        });
        Tests.push_back({
            "GiftCombo completion captures ready Gift",
            &TestGiftComboCompletionCapturesReadyGift
        });
        Tests.push_back({
            "Gift completion captures ready GiftCombo",
            &TestGiftCompletionCapturesReadyGiftCombo
        });
        Tests.push_back({
            "GiftCombo cancel advances with explicit Pump",
            &TestGiftComboCancelAdvancesWithExplicitPump
        });
        Tests.push_back({
            "Wrong-route Gift and GiftCombo completions fail before Core mutation",
            &TestWrongRouteGiftAndGiftComboCompletionsFailBeforeCoreMutation
        });
        Tests.push_back({
            "Pending GiftCombo expires while Chat is Processing",
            &TestPendingGiftComboExpiresWhileChatIsProcessing
        });
        Tests.push_back({
            "GiftCombo Failed completion is terminal and Host recovers",
            &TestGiftComboFailedCompletionIsTerminalAndHostRecovers
        });
        Tests.push_back({
            "Expirations before GiftCombo completion remain in DueExpirations",
            &TestExpirationsBeforeGiftComboCompletionRemainInDueExpirations
        });
    }
}
