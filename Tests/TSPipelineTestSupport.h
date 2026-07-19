#pragma once

#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"
#include "TSTestHarness.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace TikStudio::Tests
{

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

    inline void RequireUserEqual(
        const FTSUserSnapshot& Actual,
        const FTSUserSnapshot& Expected,
        const std::string& Context
    )
    {
        Require(Actual.UniqueId == Expected.UniqueId, Context + ": UniqueId");
        Require(Actual.Nickname == Expected.Nickname, Context + ": Nickname");
        Require(
            Actual.ProfilePictureUrl == Expected.ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(Actual.FollowRole == Expected.FollowRole, Context + ": FollowRole");
        Require(
            Actual.bIsModerator == Expected.bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            Actual.bIsSubscriber == Expected.bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(
            Actual.bIsNewGifter == Expected.bIsNewGifter,
            Context + ": bIsNewGifter"
        );
        Require(
            Actual.TopGifterRank == Expected.TopGifterRank,
            Context + ": TopGifterRank"
        );
        Require(
            Actual.GifterLevel == Expected.GifterLevel,
            Context + ": GifterLevel"
        );
        Require(
            Actual.TeamMemberLevel == Expected.TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    inline void RequireChatInputEqual(
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
                Actual.Emotes[Index].EmoteImageUrl
                    == Expected.Emotes[Index].EmoteImageUrl,
                Context + ": EmoteImageUrl"
            );
        }

        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    inline void RequireFollowInputEqual(
        const FTSFollowInput& Actual,
        const FTSFollowInput& Expected,
        const std::string& Context
    )
    {
        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    inline void RequireShareInputEqual(
        const FTSShareInput& Actual,
        const FTSShareInput& Expected,
        const std::string& Context
    )
    {
        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    inline void RequireLikeInputEqual(
        const FTSLikeInput& Actual,
        const FTSLikeInput& Expected,
        const std::string& Context
    )
    {
        Require(
            Actual.LikeCount == Expected.LikeCount,
            Context + ": LikeCount"
        );
        Require(
            Actual.TotalLikeCount == Expected.TotalLikeCount,
            Context + ": TotalLikeCount"
        );
        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    inline void RequireRoomUserTopViewerEqual(
        const FTSRoomUserTopViewer& Actual,
        const FTSRoomUserTopViewer& Expected,
        const std::string& Context
    )
    {
        Require(Actual.UniqueId == Expected.UniqueId, Context + ": UniqueId");
        Require(Actual.Nickname == Expected.Nickname, Context + ": Nickname");
        Require(
            Actual.ProfilePictureUrl == Expected.ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(Actual.CoinCount == Expected.CoinCount, Context + ": CoinCount");
        Require(
            Actual.bIsModerator == Expected.bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            Actual.bIsSubscriber == Expected.bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(
            Actual.GifterLevel == Expected.GifterLevel,
            Context + ": GifterLevel"
        );
        Require(
            Actual.TeamMemberLevel == Expected.TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    inline void RequireRoomUserInputEqual(
        const FTSRoomUserInput& Actual,
        const FTSRoomUserInput& Expected,
        const std::string& Context
    )
    {
        Require(
            Actual.ViewerCount == Expected.ViewerCount,
            Context + ": ViewerCount"
        );
        Require(
            Actual.TopGifterRank == Expected.TopGifterRank,
            Context + ": TopGifterRank"
        );
        Require(
            Actual.TopViewers.size() == Expected.TopViewers.size(),
            Context + ": TopViewers size"
        );
        for (std::size_t Index = 0; Index < Expected.TopViewers.size(); ++Index)
        {
            RequireRoomUserTopViewerEqual(
                Actual.TopViewers[Index],
                Expected.TopViewers[Index],
                Context + ": TopViewer " + std::to_string(Index)
            );
        }
    }

    inline void RequireGiftInputEqual(
        const FTSGiftInput& Actual,
        const FTSGiftInput& Expected,
        const std::string& Context
    )
    {
        Require(Actual.GiftId == Expected.GiftId, Context + ": GiftId");
        Require(Actual.GiftName == Expected.GiftName, Context + ": GiftName");
        Require(
            Actual.GiftPictureUrl == Expected.GiftPictureUrl,
            Context + ": GiftPictureUrl"
        );
        Require(
            Actual.DiamondCount == Expected.DiamondCount,
            Context + ": DiamondCount"
        );
        Require(
            Actual.RepeatCount == Expected.RepeatCount,
            Context + ": RepeatCount"
        );
        Require(Actual.GiftType == Expected.GiftType, Context + ": GiftType");
        Require(Actual.Describe == Expected.Describe, Context + ": Describe");
        Require(
            Actual.bRepeatEnd == Expected.bRepeatEnd,
            Context + ": bRepeatEnd"
        );
        Require(Actual.GroupId == Expected.GroupId, Context + ": GroupId");
        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    [[nodiscard]]
    inline FTSChatInput MakeCompleteInput()
    {
        FTSChatInput Input;
        Input.Comment = "Portable chat snapshot";
        Input.Emotes = {
            FTSEmoteInfo{"emote-wave", "https://example.test/wave.png"},
            FTSEmoteInfo{"emote-heart", "https://example.test/heart.png"}
        };
        Input.User.UniqueId = "user-42";
        Input.User.Nickname = "Chat User";
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

    [[nodiscard]]
    inline FTSFollowInput MakeCompleteFollowInput()
    {
        FTSFollowInput Input;
        Input.User = MakeCompleteInput().User;
        Input.User.UniqueId = "follow-user-42";
        Input.User.Nickname = "Follow User";
        return Input;
    }

    [[nodiscard]]
    inline FTSShareInput MakeCompleteShareInput()
    {
        FTSShareInput Input;
        Input.User.UniqueId = "share-user-42";
        Input.User.Nickname = "Share User";
        Input.User.ProfilePictureUrl = "https://example.test/share.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    [[nodiscard]]
    inline FTSLikeInput MakeCompleteLikeInput()
    {
        FTSLikeInput Input;
        Input.LikeCount = 17;
        Input.TotalLikeCount = 900;
        Input.User.UniqueId = "like-user-42";
        Input.User.Nickname = "Like User";
        Input.User.ProfilePictureUrl = "https://example.test/like.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    [[nodiscard]]
    inline FTSRoomUserInput MakeCompleteRoomUserInput()
    {
        FTSRoomUserInput Input;
        Input.ViewerCount = 123;
        Input.TopGifterRank = 7;
        Input.TopViewers = {
            FTSRoomUserTopViewer{
                "room-viewer-a",
                "Room Viewer A",
                "https://example.test/room-a.png",
                1000,
                true,
                false,
                11,
                13
            },
            FTSRoomUserTopViewer{
                "room-viewer-b",
                "Room Viewer B",
                "https://example.test/room-b.png",
                500,
                false,
                true,
                17,
                19
            }
        };
        return Input;
    }

    [[nodiscard]]
    inline FTSGiftInput MakeCompleteGiftInput()
    {
        FTSGiftInput Input;
        Input.GiftId = 5655;
        Input.GiftName = "Rose";
        Input.GiftPictureUrl = "https://example.test/gift.png";
        Input.DiamondCount = 20;
        Input.RepeatCount = 7;
        Input.GiftType = 1;
        Input.Describe = "Complete portable Gift";
        Input.bRepeatEnd = true;
        Input.GroupId = "gift-group-42";
        Input.User.UniqueId = "gift-user-42";
        Input.User.Nickname = "Gift User";
        Input.User.ProfilePictureUrl =
            "https://example.test/gift-user.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Chat settings must be available");
        ChatSettings->bEnabled = bEnabled;
        ChatSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalChatSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{8000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(true, 10);
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Chat settings must be available");
        ChatSettings->TTL = TTL;
        ChatSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeFollowSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* FollowSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Follow);
        Require(FollowSettings != nullptr, "Follow settings must be available");
        FollowSettings->bEnabled = bEnabled;
        FollowSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalFollowSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{8000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeFollowSettings(true, 10);
        FTSFlowQueueSettings* FollowSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Follow);
        Require(FollowSettings != nullptr, "Follow settings must be available");
        FollowSettings->TTL = TTL;
        FollowSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeShareSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ShareSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Share);
        Require(ShareSettings != nullptr, "Share settings must be available");
        ShareSettings->bEnabled = bEnabled;
        ShareSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalShareSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{25000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeShareSettings(true, 10);
        FTSFlowQueueSettings* ShareSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Share);
        Require(ShareSettings != nullptr, "Share settings must be available");
        ShareSettings->TTL = TTL;
        ShareSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeLikeSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* LikeSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Like);
        Require(LikeSettings != nullptr, "Like settings must be available");
        LikeSettings->bEnabled = bEnabled;
        LikeSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalLikeSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{10000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        // Se usa capacidad amplia para escenarios operativos con más de una
        // admisión. La prueba específica de capacidad fija MaxSlots = 1.
        FTSEventQueueSettings Settings = MakeLikeSettings(true, 10);
        FTSFlowQueueSettings* LikeSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Like);
        Require(LikeSettings != nullptr, "Like settings must be available");
        LikeSettings->TTL = TTL;
        LikeSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeRoomUserSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* RoomUserSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::RoomUser);
        Require(
            RoomUserSettings != nullptr,
            "RoomUser settings must be available"
        );
        RoomUserSettings->bEnabled = bEnabled;
        RoomUserSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalRoomUserSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{15000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeRoomUserSettings(true, 10);
        FTSFlowQueueSettings* RoomUserSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::RoomUser);
        Require(
            RoomUserSettings != nullptr,
            "RoomUser settings must be available"
        );
        RoomUserSettings->TTL = TTL;
        RoomUserSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeGiftSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* GiftSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Gift);
        Require(GiftSettings != nullptr, "Gift settings must be available");
        GiftSettings->bEnabled = bEnabled;
        GiftSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeOperationalGiftSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{45000},
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeGiftSettings(true, 10);
        FTSFlowQueueSettings* GiftSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Gift);
        Require(GiftSettings != nullptr, "Gift settings must be available");
        GiftSettings->TTL = TTL;
        GiftSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedChat(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& Comment
    )
    {
        FTSChatInput Input = MakeCompleteInput();
        Input.Comment = Comment;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Chat admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Chat admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedFollow(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& UniqueId
    )
    {
        FTSFollowInput Input = MakeCompleteFollowInput();
        Input.User.UniqueId = UniqueId;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitFollow(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Follow admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Follow admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedShare(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& UniqueId
    )
    {
        FTSShareInput Input = MakeCompleteShareInput();
        Input.User.UniqueId = UniqueId;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShare(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Share admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Share admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedLike(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& UniqueId
    )
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.User.UniqueId = UniqueId;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLike(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Like admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Like admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedRoomUser(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& FirstViewerUniqueId
    )
    {
        FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        Require(
            !Input.TopViewers.empty(),
            "Complete RoomUser input must contain a TopViewer"
        );
        Input.TopViewers[0].UniqueId = FirstViewerUniqueId;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitRoomUser(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "RoomUser admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted RoomUser admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId SubmitAcceptedGift(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& UniqueId
    )
    {
        FTSGiftInput Input = MakeCompleteGiftInput();
        Input.User.UniqueId = UniqueId;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGift(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Gift admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Gift admission must have a valid identity"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.Flow ==
                ETSEventFlow::Gift,
            "Accepted Gift admission must use the direct flow"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyChat(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSChatDispatchResult Dispatch =
            Coordinator.BeginChatProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Chat must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyFollow(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSFollowDispatchResult Dispatch =
            Coordinator.BeginFollowProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Follow must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyShare(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSShareDispatchResult Dispatch =
            Coordinator.BeginShareProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Share must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyLike(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSLikeDispatchResult Dispatch =
            Coordinator.BeginLikeProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Like must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyRoomUser(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSRoomUserDispatchResult Dispatch =
            Coordinator.BeginRoomUserProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready RoomUser must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    [[nodiscard]]
    inline FTSEmissionId BeginReadyGift(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSGiftDispatchResult Dispatch =
            Coordinator.BeginGiftProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Gift must produce a dispatch"
        );
        Require(
            Dispatch.Dispatch->Emission.Flow == ETSEventFlow::Gift,
            "A ready Gift must preserve the direct flow"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

}
