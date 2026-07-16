#include "TikFinity/TSTikFinityChatConverter.h"

#include <limits>
#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityChatConversionResult MakeRejectedResult(
        ETSTikFinityChatConversionStatus Status
    )
    {
        FTSTikFinityChatConversionResult Result;
        Result.Status = Status;
        return Result;
    }

    [[nodiscard]]
    bool TryConvertNumericField(
        const std::optional<std::int64_t>& Source,
        std::int32_t& Destination
    ) noexcept
    {
        if (!Source.has_value())
        {
            Destination = 0;
            return true;
        }

        if (*Source < 0 ||
            *Source > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }

        Destination = static_cast<std::int32_t>(*Source);
        return true;
    }
}

FTSTikFinityChatConversionResult FTSTikFinityChatConverter::Convert(
    const FTSTikFinityDecodedChatMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "chat")
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::IgnoredNonChatEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedChatData& Data = *Message.Data;

    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;

    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedMissingUserIdentity
        );
    }

    const std::string Comment = Data.Comment.value_or(std::string{});

    if (Comment.empty() && Data.Emotes.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedEmptyContent
        );
    }

    std::int32_t FollowRole = 0;
    std::int32_t TopGifterRank = 0;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;

    if (!TryConvertNumericField(User.FollowRole, FollowRole) ||
        !TryConvertNumericField(User.TopGifterRank, TopGifterRank) ||
        !TryConvertNumericField(User.GifterLevel, GifterLevel) ||
        !TryConvertNumericField(User.TeamMemberLevel, TeamMemberLevel))
    {
        return MakeRejectedResult(
            ETSTikFinityChatConversionStatus::RejectedInvalidNumericField
        );
    }

    // Validar toda la colección evita publicar una conversión parcial.
    for (const FTSTikFinityDecodedEmote& Emote : Data.Emotes)
    {
        if (!Emote.EmoteId.has_value() || Emote.EmoteId->empty() ||
            !Emote.EmoteImageUrl.has_value() ||
            Emote.EmoteImageUrl->empty())
        {
            return MakeRejectedResult(
                ETSTikFinityChatConversionStatus::RejectedInvalidEmote
            );
        }
    }

    FTSChatInput Input;
    Input.Comment = Comment;
    Input.Emotes.reserve(Data.Emotes.size());

    for (const FTSTikFinityDecodedEmote& Emote : Data.Emotes)
    {
        Input.Emotes.push_back(
            FTSEmoteInfo{*Emote.EmoteId, *Emote.EmoteImageUrl}
        );
    }

    Input.User.UniqueId = *User.UniqueId;
    Input.User.Nickname = User.Nickname.value_or(std::string{});
    Input.User.ProfilePictureUrl =
        User.ProfilePictureUrl.value_or(std::string{});
    Input.User.FollowRole = FollowRole;
    Input.User.bIsModerator = User.bIsModerator.value_or(false);
    Input.User.bIsSubscriber = User.bIsSubscriber.value_or(false);
    Input.User.bIsNewGifter = User.bIsNewGifter.value_or(false);
    Input.User.TopGifterRank = TopGifterRank;
    Input.User.GifterLevel = GifterLevel;
    Input.User.TeamMemberLevel = TeamMemberLevel;

    FTSTikFinityChatConversionResult Result;
    Result.Status = ETSTikFinityChatConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
