#include "EventPipeline/Families/TSChatFamily.h"

#include <chrono>
#include <limits>
#include <utility>

namespace
{
    [[nodiscard]]
    constexpr bool IsAsciiWhitespace(unsigned char Character) noexcept
    {
        return Character == ' ' || Character == '\t' ||
            Character == '\n' || Character == '\r' ||
            Character == '\f' || Character == '\v';
    }

    void AddSaturated(std::size_t& Total, std::size_t Value) noexcept
    {
        const std::size_t Maximum = std::numeric_limits<std::size_t>::max();
        Total = Value > Maximum - Total ? Maximum : Total + Value;
    }

    [[nodiscard]]
    std::size_t CalculateMessageCost(
        const FTSChatMessageEntry& Message
    ) noexcept
    {
        std::size_t Cost = Message.Comment.size();
        for (const FTSEmoteInfo& Emote : Message.Emotes)
        {
            AddSaturated(Cost, Emote.EmoteId.size());
            AddSaturated(Cost, Emote.EmoteImageUrl.size());
        }
        return Cost;
    }

    [[nodiscard]]
    std::size_t CalculatePayloadCost(const FTSChatPayload& Payload) noexcept
    {
        std::size_t Cost = Payload.User.UniqueId.size();
        AddSaturated(Cost, Payload.User.Nickname.size());
        AddSaturated(Cost, Payload.User.ProfilePictureUrl.size());
        for (const FTSChatMessageEntry& Message : Payload.Messages)
        {
            AddSaturated(Cost, CalculateMessageCost(Message));
        }
        return Cost;
    }
}

FTSChatFamilyDecision FTSChatFamily::Decide(
    FTSChatInput Input,
    FTSEventQueueTimePoint ReceivedAt,
    const FTSEventPipelineSettings& Settings
)
{
    FTSChatFamilyDecision Decision;
    const bool bIsCommand = IsCommand(Input.Comment, Settings.Chat);
    if (Settings.Chat.bOnlyAllowCommands && !bIsCommand)
    {
        return Decision;
    }

    FTSChatMessageEntry Message;
    Message.Comment = std::move(Input.Comment);
    Message.Emotes = std::move(Input.Emotes);
    Message.ReceivedAt = ReceivedAt;
    Message.bIsCommand = bIsCommand;

    if (CalculateMessageCost(Message) > Settings.Chat.MaxMessageUtf8Bytes)
    {
        Decision.Status = ETSChatFamilyDecisionStatus::RejectedSemanticLimit;
        return Decision;
    }

    if (Input.User.UniqueId.empty())
    {
        Decision.Status = ETSChatFamilyDecisionStatus::RejectedInvalidInput;
        return Decision;
    }

    TTSAdmissionCandidate<FTSChatPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Chat;
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Chat;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;
    Candidate.Payload.User = std::move(Input.User);
    Candidate.Payload.Messages.push_back(std::move(Message));

    if (!IsWithinBatchLimits(Candidate.Payload, Settings.Chat))
    {
        Decision.Status = ETSChatFamilyDecisionStatus::RejectedSemanticLimit;
        return Decision;
    }

    Decision.Status = ETSChatFamilyDecisionStatus::Candidate;
    Decision.Candidate.emplace(std::move(Candidate));
    return Decision;
}

bool FTSChatFamily::IsCommand(
    const std::string& Comment,
    const FTSChatSemanticSettings& Settings
) noexcept
{
    std::size_t Offset = 0;
    if (Settings.bAllowLeadingWhitespace)
    {
        while (Offset < Comment.size() &&
               IsAsciiWhitespace(
                   static_cast<unsigned char>(Comment[Offset])
               ))
        {
            ++Offset;
        }
    }

    if (Comment.size() - Offset < Settings.CommandPrefix.size() ||
        Comment.compare(
            Offset,
            Settings.CommandPrefix.size(),
            Settings.CommandPrefix
        ) != 0)
    {
        return false;
    }

    const std::size_t BoundaryOffset =
        Offset + Settings.CommandPrefix.size();
    return !Settings.bRequireCommandBoundary ||
        BoundaryOffset == Comment.size() ||
        IsAsciiWhitespace(
            static_cast<unsigned char>(Comment[BoundaryOffset])
        );
}

bool FTSChatFamily::IsWithinBatchLimits(
    const FTSChatPayload& Payload,
    const FTSChatSemanticSettings& Settings
) noexcept
{
    if (Payload.Messages.empty() ||
        Payload.Messages.size() > Settings.MaxMessagesPerBatch)
    {
        return false;
    }

    for (const FTSChatMessageEntry& Message : Payload.Messages)
    {
        if (CalculateMessageCost(Message) > Settings.MaxMessageUtf8Bytes)
        {
            return false;
        }
    }

    return CalculatePayloadCost(Payload) <= Settings.MaxBatchUtf8Bytes;
}
