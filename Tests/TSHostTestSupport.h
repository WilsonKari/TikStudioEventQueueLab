#pragma once

#include "EventHost/TSEventExecutionHost.h"
#include "TSTestHarness.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
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
}
