#include "TikFinity/TSTikFinityChatConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

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

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
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

    Input.User = std::move(*ConvertedUser);

    FTSTikFinityChatConversionResult Result;
    Result.Status = ETSTikFinityChatConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
