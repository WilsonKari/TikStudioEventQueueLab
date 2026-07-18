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

}
