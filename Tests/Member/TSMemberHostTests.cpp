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

    void MutateMemberInput(FTSMemberInput& Input)
    {
        Input.ActionId = 0;
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

    [[nodiscard]]
    FTSEventQueueSettings MakeChatMemberSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t MemberMaxSlots = 1,
        std::chrono::milliseconds ChatTTL = 8s,
        std::chrono::milliseconds MemberTTL = 6s
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL
        );
        FTSFlowQueueSettings* MemberSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Member);
        Require(MemberSettings != nullptr, "Member settings must exist");
        MemberSettings->bEnabled = true;
        MemberSettings->MaxSlots = MemberMaxSlots;
        MemberSettings->TTL = MemberTTL;
        MemberSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
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

    void RequireSuccessfulHostCompletion(
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

    void TestMemberInputAutoPumpsAndDispatches()
    {
        FTSEventExecutionHost Host;
        FTSMemberInput Input = MakeMemberInput("member-auto");
        const FTSMemberInput Expected = Input;

        Require(Host.PostMember(Input), "First Member publication must signal");
        MutateMemberInput(Input);

        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedMemberAdmission(Cycle, "Member Auto Pump");
        const FTSMemberProcessingDispatch& Dispatch =
            RequireMemberDispatch(Cycle, "Member Auto Pump");
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Member &&
                Dispatch.Emission.Flow != ETSEventFlow::MemberRate,
            "Member Auto Pump dispatch identity and direct flow"
        );
        RequireMemberInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Member Auto Pump owned payload"
        );
        Require(!Cycle.bMoreCommandsPending, "Member command must be consumed");

        const FTSEventHostCycleResult OneShotCycle = Host.RunOneCycle();
        Require(
            OneShotCycle.ProcessedCommand == ETSEventHostCommandKind::None &&
                !OneShotCycle.Dispatch.has_value() &&
                OneShotCycle.PumpResult.has_value() &&
                OneShotCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Member dispatch must be emitted exactly once"
        );

        Require(
            Host.PostMemberCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Member completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::MemberCompletion,
            EmissionId,
            "Member Auto Pump completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Completed Member must leave no work"
        );
    }

    void TestWorkerPostMemberRunsOnOwner()
    {
        std::atomic<int> NowCallCount{0};
        FTSNowProvider NowProvider =
            [&]()
            {
                ++NowCallCount;
                return FTSEventQueueTimePoint{};
            };
        FTSEventExecutionHost Host(FTSEventQueueSettings{}, NowProvider);
        FTSMemberInput Input = MakeMemberInput("member-worker");
        const FTSMemberInput Expected = Input;
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
                    bScheduleRequested = Host.PostMember(Input);
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
            "Worker PostMember must only request owner scheduling"
        );

        MutateMemberInput(Input);
        const FTSEventHostCycleResult Cycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedMemberAdmission(Cycle, "Worker Member");
        const FTSMemberProcessingDispatch& Dispatch =
            RequireMemberDispatch(Cycle, "Worker Member");
        Require(
            NowCallCount.load() > 0 &&
                std::this_thread::get_id() == OwnerThreadId,
            "Owner cycle must execute the Member pipeline"
        );
        RequireMemberInputEqual(
            Dispatch.Payload.Input,
            Expected,
            "Worker Member owned payload"
        );

        Require(
            Host.PostMemberCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Worker Member cleanup must signal"
        );
        RequireSuccessfulHostCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::MemberCompletion,
            EmissionId,
            "Worker Member cleanup"
        );
    }

    void TestMixedSevenFamiliesPreserveHostFifoAndCoreOrder()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("mixed-member-chat");
        const FTSFollowInput FollowInput = MakeFollowInput("mixed-member-follow");
        const FTSShareInput ShareInput = MakeShareInput("mixed-member-share");
        const FTSLikeInput LikeInput = MakeLikeInput("mixed-member-like");
        const FTSRoomUserInput RoomUserInput =
            MakeRoomUserInput("mixed-member-room");
        const FTSGiftInput GiftInput = MakeGiftInput("mixed-member-gift");
        const FTSMemberInput MemberInput = MakeMemberInput("mixed-member");

        Require(Host.PostChat(ChatInput), "Mixed Chat must signal");
        Require(!Host.PostFollow(FollowInput), "Mixed Follow must queue");
        Require(!Host.PostShare(ShareInput), "Mixed Share must queue");
        Require(!Host.PostLike(LikeInput), "Mixed Like must queue");
        Require(!Host.PostRoomUser(RoomUserInput), "Mixed RoomUser must queue");
        Require(!Host.PostGift(GiftInput), "Mixed Gift must queue");
        Require(!Host.PostMember(MemberInput), "Mixed Member must queue");

        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Seven FIFO Chat");
        Require(
            RequireChatDispatch(ChatCycle, "Seven FIFO Chat")
                    .Emission.EmissionId == ChatId &&
                ChatCycle.bMoreCommandsPending,
            "Host FIFO must process Chat first"
        );
        RequireChatInputEqual(
            RequireChatDispatch(ChatCycle, "Seven FIFO Chat").Payload.Input,
            ChatInput,
            "Seven FIFO Chat payload"
        );

        const FTSEventHostCycleResult FollowCycle = Host.RunOneCycle();
        const FTSEmissionId FollowId =
            RequireAcceptedFollowAdmission(FollowCycle, "Seven FIFO Follow");
        RequireBusyPendingCycle(FollowCycle, true, "Follow admitted second");

        const FTSEventHostCycleResult ShareCycle = Host.RunOneCycle();
        const FTSEmissionId ShareId =
            RequireAcceptedShareAdmission(ShareCycle, "Seven FIFO Share");
        RequireBusyPendingCycle(ShareCycle, true, "Share admitted third");

        const FTSEventHostCycleResult LikeCycle = Host.RunOneCycle();
        const FTSEmissionId LikeId =
            RequireAcceptedLikeAdmission(LikeCycle, "Seven FIFO Like");
        RequireBusyPendingCycle(LikeCycle, true, "Like admitted fourth");

        const FTSEventHostCycleResult RoomUserCycle = Host.RunOneCycle();
        const FTSEmissionId RoomUserId = RequireAcceptedRoomUserAdmission(
            RoomUserCycle,
            "Seven FIFO RoomUser"
        );
        RequireBusyPendingCycle(RoomUserCycle, true, "RoomUser admitted fifth");

        const FTSEventHostCycleResult GiftCycle = Host.RunOneCycle();
        const FTSEmissionId GiftId =
            RequireAcceptedGiftAdmission(GiftCycle, "Seven FIFO Gift");
        RequireBusyPendingCycle(GiftCycle, true, "Gift admitted sixth");

        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(MemberCycle, "Seven FIFO Member");
        RequireBusyPendingCycle(MemberCycle, false, "Member admitted seventh");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Seven Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            ChatCompletion,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Seven Chat completion"
        );
        const FTSGiftProcessingDispatch& GiftDispatch =
            RequireGiftDispatch(ChatCompletion, "Core selects Gift");
        Require(GiftDispatch.Emission.EmissionId == GiftId, "Gift priority");
        RequireGiftInputEqual(
            GiftDispatch.Payload.Input,
            GiftInput,
            "Seven Gift payload"
        );

        Require(
            Host.PostGiftCompletion(GiftId, ETSProcessingResult::Succeeded),
            "Seven Gift completion must signal"
        );
        const FTSEventHostCycleResult GiftCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            GiftCompletion,
            ETSEventHostCommandKind::GiftCompletion,
            GiftId,
            "Seven Gift completion"
        );
        const FTSFollowProcessingDispatch& FollowDispatch =
            RequireFollowDispatch(GiftCompletion, "Core selects Follow");
        Require(FollowDispatch.Emission.EmissionId == FollowId, "Follow priority");
        RequireFollowInputEqual(
            FollowDispatch.Payload.Input,
            FollowInput,
            "Seven Follow payload"
        );

        Require(
            Host.PostFollowCompletion(FollowId, ETSProcessingResult::Succeeded),
            "Seven Follow completion must signal"
        );
        const FTSEventHostCycleResult FollowCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            FollowCompletion,
            ETSEventHostCommandKind::FollowCompletion,
            FollowId,
            "Seven Follow completion"
        );
        const FTSShareProcessingDispatch& ShareDispatch =
            RequireShareDispatch(FollowCompletion, "Core selects Share");
        Require(ShareDispatch.Emission.EmissionId == ShareId, "Share priority");
        RequireShareInputEqual(
            ShareDispatch.Payload.Input,
            ShareInput,
            "Seven Share payload"
        );

        Require(
            Host.PostShareCompletion(ShareId, ETSProcessingResult::Succeeded),
            "Seven Share completion must signal"
        );
        const FTSEventHostCycleResult ShareCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            ShareCompletion,
            ETSEventHostCommandKind::ShareCompletion,
            ShareId,
            "Seven Share completion"
        );
        const FTSRoomUserProcessingDispatch& RoomUserDispatch =
            RequireRoomUserDispatch(ShareCompletion, "Core selects RoomUser");
        Require(
            RoomUserDispatch.Emission.EmissionId == RoomUserId,
            "RoomUser priority"
        );
        RequireRoomUserInputEqual(
            RoomUserDispatch.Payload.Input,
            RoomUserInput,
            "Seven RoomUser payload"
        );

        Require(
            Host.PostRoomUserCompletion(
                RoomUserId,
                ETSProcessingResult::Succeeded
            ),
            "Seven RoomUser completion must signal"
        );
        const FTSEventHostCycleResult RoomUserCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            RoomUserCompletion,
            ETSEventHostCommandKind::RoomUserCompletion,
            RoomUserId,
            "Seven RoomUser completion"
        );
        const FTSLikeProcessingDispatch& LikeDispatch =
            RequireLikeDispatch(RoomUserCompletion, "Core selects Like");
        Require(LikeDispatch.Emission.EmissionId == LikeId, "Like priority");
        RequireLikeInputEqual(
            LikeDispatch.Payload.Input,
            LikeInput,
            "Seven Like payload"
        );

        Require(
            Host.PostLikeCompletion(LikeId, ETSProcessingResult::Succeeded),
            "Seven Like completion must signal"
        );
        const FTSEventHostCycleResult LikeCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            LikeCompletion,
            ETSEventHostCommandKind::LikeCompletion,
            LikeId,
            "Seven Like completion"
        );
        const FTSMemberProcessingDispatch& MemberDispatch =
            RequireMemberDispatch(LikeCompletion, "Core selects Member");
        Require(
            MemberDispatch.Emission.EmissionId == MemberId &&
                MemberDispatch.Emission.Flow == ETSEventFlow::Member &&
                MemberDispatch.Emission.Flow !=
                    ETSEventFlow::MemberRate,
            "Member must be the final direct-flow dispatch"
        );
        RequireMemberInputEqual(
            MemberDispatch.Payload.Input,
            MemberInput,
            "Seven Member payload"
        );

        Require(
            Host.PostMemberCompletion(MemberId, ETSProcessingResult::Succeeded),
            "Seven Member completion must signal"
        );
        const FTSEventHostCycleResult MemberCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            MemberCompletion,
            ETSEventHostCommandKind::MemberCompletion,
            MemberId,
            "Seven Member completion"
        );
        Require(
            !MemberCompletion.Dispatch.has_value() &&
                !MemberCompletion.bMoreCommandsPending,
            "Seven-family scenario must leave no work"
        );
    }

    void TestMemberCompletionCapturesReadyChat()
    {
        FTSEventExecutionHost Host;
        const FTSMemberInput MemberInput = MakeMemberInput("member-then-chat");
        const FTSChatInput ChatInput = MakeChatInput("chat-after-member");

        Require(Host.PostMember(MemberInput), "Processing Member must signal");
        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(MemberCycle, "Processing Member");
        RequireMemberInputEqual(
            RequireMemberDispatch(MemberCycle, "Processing Member")
                .Payload.Input,
            MemberInput,
            "Processing Member payload"
        );

        Require(Host.PostChat(ChatInput), "Pending Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Pending Chat");
        RequireBusyPendingCycle(ChatCycle, false, "Chat waits behind Member");

        Require(
            Host.PostMemberCompletion(MemberId, ETSProcessingResult::Succeeded),
            "Member completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::MemberCompletion,
            MemberId,
            "Member completion captures Chat"
        );
        const FTSChatProcessingDispatch& ChatDispatch =
            RequireChatDispatch(CompletionCycle, "Chat captured after Member");
        Require(ChatDispatch.Emission.EmissionId == ChatId, "Captured Chat ID");
        RequireChatInputEqual(
            ChatDispatch.Payload.Input,
            ChatInput,
            "Captured Chat payload"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Captured Chat cleanup must signal"
        );
        RequireSuccessfulHostCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Captured Chat cleanup"
        );
    }

    void TestChatCompletionCapturesReadyMember()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("chat-then-member");
        const FTSMemberInput MemberInput = MakeMemberInput("member-after-chat");

        Require(Host.PostChat(ChatInput), "Processing Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Processing Chat");
        RequireChatInputEqual(
            RequireChatDispatch(ChatCycle, "Processing Chat").Payload.Input,
            ChatInput,
            "Processing Chat payload"
        );

        Require(Host.PostMember(MemberInput), "Pending Member must signal");
        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(MemberCycle, "Pending Member");
        RequireBusyPendingCycle(MemberCycle, false, "Member waits behind Chat");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Chat completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Chat completion captures Member"
        );
        const FTSMemberProcessingDispatch& MemberDispatch =
            RequireMemberDispatch(CompletionCycle, "Member captured after Chat");
        Require(MemberDispatch.Emission.EmissionId == MemberId, "Captured Member ID");
        RequireMemberInputEqual(
            MemberDispatch.Payload.Input,
            MemberInput,
            "Captured Member payload"
        );

        const FTSEventHostCycleResult OneShotCycle = Host.RunOneCycle();
        Require(
            !OneShotCycle.Dispatch.has_value() &&
                OneShotCycle.PumpResult.has_value() &&
                OneShotCycle.PumpResult->Outcome.Status == ETSPumpStatus::Busy,
            "Captured Member dispatch must be one-shot"
        );

        Require(
            Host.PostMemberCompletion(MemberId, ETSProcessingResult::Succeeded),
            "Captured Member cleanup must signal"
        );
        RequireSuccessfulHostCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::MemberCompletion,
            MemberId,
            "Captured Member cleanup"
        );
    }

    void TestMemberCancelAdvancesWithExplicitPump()
    {
        FTSEventExecutionHost Host;
        const FTSMemberInput FirstInput = MakeMemberInput("member-cancel-first");
        const FTSMemberInput SecondInput = MakeMemberInput("member-cancel-second");

        Require(Host.PostMember(FirstInput), "First Member must signal");
        Require(!Host.PostMember(SecondInput), "Second Member must queue");

        const FTSEventHostCycleResult FirstCycle = Host.RunOneCycle();
        const FTSEmissionId FirstId =
            RequireAcceptedMemberAdmission(FirstCycle, "First cancel Member");
        RequireMemberInputEqual(
            RequireMemberDispatch(FirstCycle, "First cancel Member")
                .Payload.Input,
            FirstInput,
            "First cancel Member payload"
        );

        const FTSEventHostCycleResult SecondCycle = Host.RunOneCycle();
        const FTSEmissionId SecondId =
            RequireAcceptedMemberAdmission(SecondCycle, "Second cancel Member");
        RequireBusyPendingCycle(SecondCycle, false, "Second Member pending");

        Require(
            Host.PostMemberCompletion(FirstId, ETSProcessingResult::Cancelled),
            "First Member cancellation must signal"
        );
        const FTSEventHostCycleResult CancelCycle = Host.RunOneCycle();
        RequireCancelledCompletion(
            RequireCompletion(
                CancelCycle,
                ETSEventHostCommandKind::MemberCompletion,
                FirstId,
                ETSProcessingResult::Cancelled,
                "First Member cancellation"
            ),
            FirstId,
            "First Member cancellation"
        );
        Require(
            CancelCycle.PumpResult.has_value() &&
                CancelCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CancelCycle.PumpResult->Outcome.ReadyEmission.EmissionId ==
                    SecondId,
            "Member Cancel must advance through explicit Host Pump"
        );
        const FTSMemberProcessingDispatch& SecondDispatch =
            RequireMemberDispatch(CancelCycle, "Second Member after cancel");
        RequireMemberInputEqual(
            SecondDispatch.Payload.Input,
            SecondInput,
            "Second Member payload"
        );

        Require(
            Host.PostMemberCompletion(SecondId, ETSProcessingResult::Succeeded),
            "Second Member cleanup must signal"
        );
        RequireSuccessfulHostCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::MemberCompletion,
            SecondId,
            "Second Member cleanup"
        );
    }

    void TestWrongFamilyMemberCompletionFailsBeforeCoreMutation()
    {
        FTSEventExecutionHost Host;
        const FTSChatInput ChatInput = MakeChatInput("wrong-member-chat");
        Require(Host.PostChat(ChatInput), "Wrong-family Chat must signal");

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Wrong-family Member completion Chat"
        );
        RequireChatInputEqual(
            RequireChatDispatch(
                AdmissionCycle,
                "Wrong-family Member completion Chat"
            ).Payload.Input,
            ChatInput,
            "Wrong-family preserved Chat payload"
        );

        Require(
            Host.PostMemberCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Wrong-family Member completion must publish"
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
        Require(bThrewLogicError, "Wrong Member completion must fail on owner");

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Correct Chat completion must still publish"
        );
        const FTSEventHostCycleResult RecoveryCycle = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            RecoveryCycle,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Wrong Member completion recovery"
        );
        Require(
            RecoveryCycle.CompletionResult->ConfirmResult->LifecycleEvents
                    .size() == 1 &&
                !RecoveryCycle.Dispatch.has_value(),
            "Wrong Member completion must not mutate Core"
        );
    }

    void TestPendingMemberExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventExecutionHost Host(
            MakeChatMemberSettings(10, 1, 8s, 5s),
            Clock.MakeProvider()
        );

        const FTSChatInput ChatInput = MakeChatInput("member-expiry-chat");
        Require(Host.PostChat(ChatInput), "Expiry Chat must signal");
        const FTSEventHostCycleResult ChatCycle = Host.RunOneCycle();
        const FTSEmissionId ChatId =
            RequireAcceptedChatAdmission(ChatCycle, "Member expiry Chat");
        RequireChatInputEqual(
            RequireChatDispatch(ChatCycle, "Member expiry Chat").Payload.Input,
            ChatInput,
            "Member expiry Chat payload"
        );

        const FTSMemberInput ExpiringInput =
            MakeMemberInput("expiring-member");
        Require(Host.PostMember(ExpiringInput), "Expiring Member must signal");
        const FTSEventHostCycleResult MemberCycle = Host.RunOneCycle();
        const FTSEmissionId ExpiringId =
            RequireAcceptedMemberAdmission(MemberCycle, "Expiring Member");
        Require(
            !MemberCycle.Dispatch.has_value() &&
                MemberCycle.NextWakeTime.Status ==
                    ETSNextWakeStatus::WakeScheduled,
            "Pending Member must schedule expiration"
        );

        Clock.Advance(6s);
        const FTSMemberInput ReplacementInput =
            MakeMemberInput("member-after-expiry");
        Require(Host.PostMember(ReplacementInput), "Replacement Member must signal");
        const FTSEventHostCycleResult ReplacementCycle = Host.RunOneCycle();
        const FTSEmissionId ReplacementId = RequireAcceptedMemberAdmission(
            ReplacementCycle,
            "Member after expiration"
        );
        const FTSEmissionLifecycleEvents& Lifecycle =
            ReplacementCycle.DueExpirations.LifecycleEvents;
        Require(
            Lifecycle.size() == 1 &&
                Lifecycle.front().Envelope.EmissionId == ExpiringId &&
                Lifecycle.front().Envelope.Flow ==
                    ETSEventFlow::Member &&
                Lifecycle.front().Envelope.Flow !=
                    ETSEventFlow::MemberRate &&
                Lifecycle.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Replacement cycle must report expired Member"
        );
        RequireBusyPendingCycle(
            ReplacementCycle,
            false,
            "Replacement Member waits behind Chat"
        );

        Require(
            Host.PostChatCompletion(ChatId, ETSProcessingResult::Succeeded),
            "Expiry Chat completion must signal"
        );
        const FTSEventHostCycleResult ChatCompletion = Host.RunOneCycle();
        RequireSuccessfulHostCompletion(
            ChatCompletion,
            ETSEventHostCommandKind::ChatCompletion,
            ChatId,
            "Expiry Chat completion"
        );
        const FTSMemberProcessingDispatch& ReplacementDispatch =
            RequireMemberDispatch(ChatCompletion, "Replacement Member dispatch");
        Require(
            ReplacementDispatch.Emission.EmissionId == ReplacementId,
            "Replacement Member identity"
        );
        RequireMemberInputEqual(
            ReplacementDispatch.Payload.Input,
            ReplacementInput,
            "Replacement Member payload"
        );

        Require(
            Host.PostMemberCompletion(
                ReplacementId,
                ETSProcessingResult::Succeeded
            ),
            "Replacement Member completion must signal"
        );
        RequireSuccessfulHostCompletion(
            Host.RunOneCycle(),
            ETSEventHostCommandKind::MemberCompletion,
            ReplacementId,
            "Replacement Member cleanup"
        );
    }

    void TestMemberFailedCompletionIsTerminalAndHostRecovers()
    {
        const FTSMemberInput FailedInput = MakeMemberInput("failed-member");
        const FTSMemberInput RecoveryInput =
            MakeMemberInput("after-failed-member");
        std::size_t DispatchIndex = 0;

        RunFailedCompletionHostScenario(
            FailedInput,
            RecoveryInput,
            ETSEventHostCommandKind::MemberCompletion,
            [](FTSEventExecutionHost& Host, FTSMemberInput Input)
            {
                return Host.PostMember(std::move(Input));
            },
            [](FTSEventExecutionHost& Host,
               FTSEmissionId EmissionId,
               ETSProcessingResult Result)
            {
                return Host.PostMemberCompletion(EmissionId, Result);
            },
            [](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                return RequireAcceptedMemberAdmission(Cycle, Context);
            },
            [&](const FTSEventHostCycleResult& Cycle, const std::string& Context)
            {
                const FTSMemberProcessingDispatch& Dispatch =
                    RequireMemberDispatch(Cycle, Context);
                const FTSMemberInput& Expected =
                    DispatchIndex++ == 0 ? FailedInput : RecoveryInput;
                RequireMemberInputEqual(
                    Dispatch.Payload.Input,
                    Expected,
                    Context + ": payload"
                );
                return Dispatch.Emission.EmissionId;
            },
            "Member Failed"
        );
        Require(DispatchIndex == 2, "Failed scenario must dispatch twice");
    }
}

namespace TikStudio::Tests
{
    void RegisterMemberHostTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Member input Auto Pumps and dispatches",
            &TestMemberInputAutoPumpsAndDispatches
        });
        Tests.push_back({
            "Worker PostMember runs on owner",
            &TestWorkerPostMemberRunsOnOwner
        });
        Tests.push_back({
            "Seven families preserve Host FIFO and Core order",
            &TestMixedSevenFamiliesPreserveHostFifoAndCoreOrder
        });
        Tests.push_back({
            "Member completion captures ready Chat",
            &TestMemberCompletionCapturesReadyChat
        });
        Tests.push_back({
            "Chat completion captures ready Member",
            &TestChatCompletionCapturesReadyMember
        });
        Tests.push_back({
            "Member cancel advances with explicit Pump",
            &TestMemberCancelAdvancesWithExplicitPump
        });
        Tests.push_back({
            "Wrong-family Member completion fails before Core mutation",
            &TestWrongFamilyMemberCompletionFailsBeforeCoreMutation
        });
        Tests.push_back({
            "Pending Member expires while Chat is Processing",
            &TestPendingMemberExpiresWhileChatIsProcessing
        });
        Tests.push_back({
            "Member Failed completion is terminal and Host recovers",
            &TestMemberFailedCompletionIsTerminalAndHostRecovers
        });
    }
}
