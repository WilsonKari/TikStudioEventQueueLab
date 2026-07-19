#pragma once

#include "EventHost/TSEventExecutionHost.h"
#include "TSTestHarness.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

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

    [[nodiscard]]
    inline FTSChatInput MakeChatInput(const std::string& Label)
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

    [[nodiscard]]
    inline FTSFollowInput MakeFollowInput(const std::string& Label)
    {
        FTSFollowInput Input;
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl = "https://example.test/follow.png";
        Input.User.FollowRole = 4;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 8;
        Input.User.GifterLevel = 12;
        Input.User.TeamMemberLevel = 14;
        return Input;
    }

    [[nodiscard]]
    inline FTSShareInput MakeShareInput(const std::string& Label)
    {
        FTSShareInput Input;
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl = "https://example.test/share.png";
        Input.User.FollowRole = 4;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 8;
        Input.User.GifterLevel = 12;
        Input.User.TeamMemberLevel = 14;
        return Input;
    }

    [[nodiscard]]
    inline FTSLikeInput MakeLikeInput(const std::string& Label)
    {
        FTSLikeInput Input;
        Input.LikeCount = 5;
        Input.TotalLikeCount = 50;
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl =
            "https://example.test/like.png";
        Input.User.FollowRole = 4;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 8;
        Input.User.GifterLevel = 12;
        Input.User.TeamMemberLevel = 14;
        return Input;
    }

    [[nodiscard]]
    inline FTSRoomUserInput MakeRoomUserInput(const std::string& Label)
    {
        FTSRoomUserInput Input;
        Input.ViewerCount = 123;
        Input.TopGifterRank = 7;
        Input.TopViewers = {
            FTSRoomUserTopViewer{
                Label + "-viewer-a",
                Label + " Viewer A",
                "https://example.test/room-a.png",
                1000,
                true,
                false,
                11,
                13
            },
            FTSRoomUserTopViewer{
                Label + "-viewer-b",
                Label + " Viewer B",
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
    inline FTSGiftInput MakeGiftInput(const std::string& Label)
    {
        FTSGiftInput Input;
        Input.GiftId = 5655;
        Input.GiftName = Label + " Gift";
        Input.GiftPictureUrl = "https://example.test/gift.png";
        Input.DiamondCount = 20;
        Input.RepeatCount = 7;
        Input.GiftType = 1;
        Input.Describe = Label + " portable Gift";
        Input.bRepeatEnd = true;
        Input.GroupId = Label + "-gift-group";
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl =
            "https://example.test/gift-user.png";
        Input.User.FollowRole = 4;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 8;
        Input.User.GifterLevel = 12;
        Input.User.TeamMemberLevel = 14;
        return Input;
    }

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
                Actual.Emotes[Index].EmoteImageUrl ==
                    Expected.Emotes[Index].EmoteImageUrl,
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
    inline FTSEventQueueSettings MakeChatSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{8000},
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
    inline FTSEventQueueSettings MakeFollowSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{8000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* FollowSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Follow);
        Require(FollowSettings != nullptr, "Follow settings must exist");
        FollowSettings->bEnabled = true;
        FollowSettings->MaxSlots = MaxSlots;
        FollowSettings->TTL = TTL;
        FollowSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeShareSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{25000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ShareSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Share);
        Require(ShareSettings != nullptr, "Share settings must exist");
        ShareSettings->bEnabled = true;
        ShareSettings->MaxSlots = MaxSlots;
        ShareSettings->TTL = TTL;
        ShareSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeLikeSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{10000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* LikeSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Like);
        Require(LikeSettings != nullptr, "Like settings must exist");
        LikeSettings->bEnabled = true;
        LikeSettings->MaxSlots = MaxSlots;
        LikeSettings->TTL = TTL;
        LikeSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeRoomUserSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{15000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* RoomUserSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::RoomUser);
        Require(RoomUserSettings != nullptr, "RoomUser settings must exist");
        RoomUserSettings->bEnabled = true;
        RoomUserSettings->MaxSlots = MaxSlots;
        RoomUserSettings->TTL = TTL;
        RoomUserSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeGiftSettings(
        std::uint32_t MaxSlots = 10,
        std::chrono::milliseconds TTL = std::chrono::milliseconds{45000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* GiftSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Gift);
        Require(GiftSettings != nullptr, "Gift settings must exist");
        GiftSettings->bEnabled = true;
        GiftSettings->MaxSlots = MaxSlots;
        GiftSettings->TTL = TTL;
        GiftSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatFollowSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t FollowMaxSlots = 10,
        std::chrono::milliseconds ChatTTL = std::chrono::milliseconds{8000},
        std::chrono::milliseconds FollowTTL = std::chrono::milliseconds{8000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL,
            bPumpAfterEnqueue,
            bPumpAfterConfirm
        );
        FTSFlowQueueSettings* FollowSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Follow);
        Require(FollowSettings != nullptr, "Follow settings must exist");
        FollowSettings->bEnabled = true;
        FollowSettings->MaxSlots = FollowMaxSlots;
        FollowSettings->TTL = FollowTTL;
        FollowSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatShareSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t ShareMaxSlots = 10,
        std::chrono::milliseconds ChatTTL = std::chrono::milliseconds{8000},
        std::chrono::milliseconds ShareTTL = std::chrono::milliseconds{25000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL,
            bPumpAfterEnqueue,
            bPumpAfterConfirm
        );
        FTSFlowQueueSettings* ShareSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Share);
        Require(ShareSettings != nullptr, "Share settings must exist");
        ShareSettings->bEnabled = true;
        ShareSettings->MaxSlots = ShareMaxSlots;
        ShareSettings->TTL = ShareTTL;
        ShareSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatLikeSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t LikeMaxSlots = 1,
        std::chrono::milliseconds ChatTTL = std::chrono::milliseconds{8000},
        std::chrono::milliseconds LikeTTL = std::chrono::milliseconds{10000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL,
            bPumpAfterEnqueue,
            bPumpAfterConfirm
        );
        FTSFlowQueueSettings* LikeSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Like);
        Require(LikeSettings != nullptr, "Like settings must exist");
        LikeSettings->bEnabled = true;
        LikeSettings->MaxSlots = LikeMaxSlots;
        LikeSettings->TTL = LikeTTL;
        LikeSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatRoomUserSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t RoomUserMaxSlots = 1,
        std::chrono::milliseconds ChatTTL = std::chrono::milliseconds{8000},
        std::chrono::milliseconds RoomUserTTL =
            std::chrono::milliseconds{15000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL,
            bPumpAfterEnqueue,
            bPumpAfterConfirm
        );
        FTSFlowQueueSettings* RoomUserSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::RoomUser);
        Require(RoomUserSettings != nullptr, "RoomUser settings must exist");
        RoomUserSettings->bEnabled = true;
        RoomUserSettings->MaxSlots = RoomUserMaxSlots;
        RoomUserSettings->TTL = RoomUserTTL;
        RoomUserSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEventQueueSettings MakeChatGiftSettings(
        std::uint32_t ChatMaxSlots = 10,
        std::uint32_t GiftMaxSlots = 1,
        std::chrono::milliseconds ChatTTL = std::chrono::milliseconds{8000},
        std::chrono::milliseconds GiftTTL = std::chrono::milliseconds{45000},
        bool bPumpAfterEnqueue = true,
        bool bPumpAfterConfirm = true
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            ChatMaxSlots,
            ChatTTL,
            bPumpAfterEnqueue,
            bPumpAfterConfirm
        );
        FTSFlowQueueSettings* GiftSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Gift);
        Require(GiftSettings != nullptr, "Gift settings must exist");
        GiftSettings->bEnabled = true;
        GiftSettings->MaxSlots = GiftMaxSlots;
        GiftSettings->TTL = GiftTTL;
        GiftSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;
        return Settings;
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedAdmission(
        const FTSEventHostCycleResult& Cycle,
        ETSEventHostCommandKind ExpectedCommand,
        const std::string& Context
    )
    {
        Require(
            Cycle.ProcessedCommand == ExpectedCommand,
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
    inline FTSEmissionId RequireAcceptedChatAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::ChatInput,
            Context
        );
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedFollowAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::FollowInput,
            Context
        );
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedShareAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::ShareInput,
            Context
        );
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedLikeAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::LikeInput,
            Context
        );
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedRoomUserAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::RoomUserInput,
            Context
        );
    }

    [[nodiscard]]
    inline FTSEmissionId RequireAcceptedGiftAdmission(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        return RequireAcceptedAdmission(
            Cycle,
            ETSEventHostCommandKind::GiftInput,
            Context
        );
    }

    [[nodiscard]]
    inline const FTSChatProcessingDispatch& RequireChatDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSChatProcessingDispatch* Dispatch =
            std::get_if<FTSChatProcessingDispatch>(&*Cycle.Dispatch);
        Require(Dispatch != nullptr, Context + ": Chat dispatch expected");
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::Chat,
            Context + ": Chat dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSFollowProcessingDispatch& RequireFollowDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSFollowProcessingDispatch* Dispatch =
            std::get_if<FTSFollowProcessingDispatch>(&*Cycle.Dispatch);
        Require(Dispatch != nullptr, Context + ": Follow dispatch expected");
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::Follow,
            Context + ": Follow dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSShareProcessingDispatch& RequireShareDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSShareProcessingDispatch* Dispatch =
            std::get_if<FTSShareProcessingDispatch>(&*Cycle.Dispatch);
        Require(Dispatch != nullptr, Context + ": Share dispatch expected");
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::Share,
            Context + ": Share dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSLikeProcessingDispatch& RequireLikeDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSLikeProcessingDispatch* Dispatch =
            std::get_if<FTSLikeProcessingDispatch>(&*Cycle.Dispatch);
        Require(Dispatch != nullptr, Context + ": Like dispatch expected");
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::Like,
            Context + ": Like dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSRoomUserProcessingDispatch& RequireRoomUserDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSRoomUserProcessingDispatch* Dispatch =
            std::get_if<FTSRoomUserProcessingDispatch>(&*Cycle.Dispatch);
        Require(
            Dispatch != nullptr,
            Context + ": RoomUser dispatch expected"
        );
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::RoomUser,
            Context + ": RoomUser dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSGiftProcessingDispatch& RequireGiftDispatch(
        const FTSEventHostCycleResult& Cycle,
        const std::string& Context
    )
    {
        Require(Cycle.Dispatch.has_value(), Context + ": dispatch expected");
        const FTSGiftProcessingDispatch* Dispatch =
            std::get_if<FTSGiftProcessingDispatch>(&*Cycle.Dispatch);
        Require(Dispatch != nullptr, Context + ": Gift dispatch expected");
        Require(
            Dispatch->Emission.EmissionId != 0 &&
                Dispatch->Emission.Flow == ETSEventFlow::Gift,
            Context + ": Gift dispatch identity and flow"
        );
        return *Dispatch;
    }

    [[nodiscard]]
    inline const FTSProcessingCompletionResult& RequireCompletion(
        const FTSEventHostCycleResult& Cycle,
        ETSEventHostCommandKind ExpectedCommand,
        FTSEmissionId ExpectedEmissionId,
        ETSProcessingResult ExpectedResult,
        const std::string& Context
    )
    {
        Require(
            Cycle.ProcessedCommand == ExpectedCommand,
            Context + ": completion command kind"
        );
        Require(
            !Cycle.AdmissionResult.has_value() &&
                Cycle.CompletionResult.has_value(),
            Context + ": completion result invariant"
        );
        Require(
            Cycle.CompletionResult->EmissionId == ExpectedEmissionId &&
                Cycle.CompletionResult->ProcessingResult == ExpectedResult,
            Context + ": completion identity and result"
        );
        return *Cycle.CompletionResult;
    }

    inline void RequireConfirmedCompletion(
        const FTSProcessingCompletionResult& Completion,
        FTSEmissionId EmissionId,
        const std::string& Context
    )
    {
        Require(
            Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value() &&
                Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed,
            Context + ": confirmed result invariant"
        );
        bool bFoundTerminal = false;
        for (const FTSEmissionLifecycleEvent& Event :
             Completion.ConfirmResult->LifecycleEvents)
        {
            if (Event.Envelope.EmissionId == EmissionId &&
                Event.Reason == ETSEmissionTerminalReason::Confirmed)
            {
                bFoundTerminal = true;
            }
        }
        Require(bFoundTerminal, Context + ": Confirmed lifecycle event");
    }

    inline void RequireCancelledCompletion(
        const FTSProcessingCompletionResult& Completion,
        FTSEmissionId EmissionId,
        const std::string& Context
    )
    {
        Require(
            !Completion.ConfirmResult.has_value() &&
                Completion.CancelResult.has_value() &&
                Completion.CancelResult->Status ==
                    ETSCancelInFlightStatus::Cancelled,
            Context + ": cancelled result invariant"
        );
        Require(
            Completion.CancelResult->LifecycleEvents.size() == 1 &&
                Completion.CancelResult->LifecycleEvents[0]
                        .Envelope.EmissionId == EmissionId &&
                Completion.CancelResult->LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Cancelled,
            Context + ": Cancelled lifecycle event"
        );
    }

    template <
        typename TInput,
        typename TPostInput,
        typename TPostCompletion,
        typename TRequireAdmission,
        typename TRequireDispatch
    >
    inline void RunFailedCompletionHostScenario(
        TInput FailedInput,
        TInput RecoveryInput,
        ETSEventHostCommandKind CompletionCommand,
        TPostInput&& PostInput,
        TPostCompletion&& PostCompletion,
        TRequireAdmission&& RequireAdmission,
        TRequireDispatch&& RequireDispatch,
        const std::string& Context
    )
    {
        FTSEventExecutionHost Host;
        Require(
            PostInput(Host, std::move(FailedInput)),
            Context + ": initial input must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId FailedEmissionId =
            RequireAdmission(AdmissionCycle, Context + " admission");
        Require(
            RequireDispatch(AdmissionCycle, Context + " dispatch") ==
                FailedEmissionId,
            Context + ": initial dispatch identity"
        );

        Require(
            PostCompletion(
                Host,
                FailedEmissionId,
                ETSProcessingResult::Failed
            ),
            Context + ": Failed completion must signal"
        );
        const FTSEventHostCycleResult FailedCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& FailedCompletion =
            RequireCompletion(
                FailedCycle,
                CompletionCommand,
                FailedEmissionId,
                ETSProcessingResult::Failed,
                Context + " completion"
            );
        RequireCancelledCompletion(
            FailedCompletion,
            FailedEmissionId,
            Context + " completion"
        );
        Require(
            !FailedCycle.Dispatch.has_value() &&
                FailedCycle.PumpResult.has_value() &&
                FailedCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::QueueEmpty,
            Context + ": Failed must be terminal without implicit retry"
        );

        Require(
            PostCompletion(
                Host,
                FailedEmissionId,
                ETSProcessingResult::Failed
            ),
            Context + ": duplicate completion must signal"
        );
        bool bDuplicateRejected = false;
        try
        {
            (void)Host.RunOneCycle();
        }
        catch (const std::logic_error&)
        {
            bDuplicateRejected = true;
        }
        Require(
            bDuplicateRejected,
            Context + ": terminal binding must be removed exactly once"
        );

        Require(
            PostInput(Host, std::move(RecoveryInput)),
            Context + ": recovery input must signal"
        );
        const FTSEventHostCycleResult RecoveryCycle = Host.RunOneCycle();
        const FTSEmissionId RecoveryEmissionId =
            RequireAdmission(RecoveryCycle, Context + " recovery admission");
        Require(
            RequireDispatch(RecoveryCycle, Context + " recovery dispatch") ==
                RecoveryEmissionId,
            Context + ": Host must continue after Failed"
        );

        Require(
            PostCompletion(
                Host,
                RecoveryEmissionId,
                ETSProcessingResult::Succeeded
            ),
            Context + ": recovery completion must signal"
        );
        const FTSEventHostCycleResult CleanupCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CleanupCycle,
                CompletionCommand,
                RecoveryEmissionId,
                ETSProcessingResult::Succeeded,
                Context + " recovery completion"
            ),
            RecoveryEmissionId,
            Context + " recovery completion"
        );
    }
}
