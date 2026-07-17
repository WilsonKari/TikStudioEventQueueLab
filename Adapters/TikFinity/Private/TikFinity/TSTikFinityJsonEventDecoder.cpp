#include "TikFinity/TSTikFinityJsonEventDecoder.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using FJson = nlohmann::json;

    void Reject(
        FTSTikFinityJsonDecodeResult& Result,
        ETSTikFinityJsonDecodeStatus Status,
        std::string ErrorPath
    )
    {
        Result.Status = Status;
        Result.ErrorPath = std::move(ErrorPath);
        Result.Event.reset();
    }

    [[nodiscard]]
    bool ReadOptionalString(
        const FJson& Object,
        std::string_view Key,
        std::string ErrorPath,
        std::optional<std::string>& Destination,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const auto Field = Object.find(std::string(Key));
        if (Field == Object.end())
        {
            return true;
        }

        if (!Field->is_string())
        {
            Reject(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                std::move(ErrorPath)
            );
            return false;
        }

        Destination = Field->get<std::string>();
        return true;
    }

    [[nodiscard]]
    bool ReadOptionalBoolean(
        const FJson& Object,
        std::string_view Key,
        std::string ErrorPath,
        std::optional<bool>& Destination,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const auto Field = Object.find(std::string(Key));
        if (Field == Object.end())
        {
            return true;
        }

        if (!Field->is_boolean())
        {
            Reject(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                std::move(ErrorPath)
            );
            return false;
        }

        Destination = Field->get<bool>();
        return true;
    }

    [[nodiscard]]
    bool ReadOptionalInteger(
        const FJson& Object,
        std::string_view Key,
        std::string ErrorPath,
        std::optional<std::int64_t>& Destination,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const auto Field = Object.find(std::string(Key));
        if (Field == Object.end())
        {
            return true;
        }

        if (Field->is_number_unsigned())
        {
            const std::uint64_t Value = Field->get<std::uint64_t>();
            if (Value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()
                ))
            {
                Reject(
                    Result,
                    ETSTikFinityJsonDecodeStatus::RejectedNumericOutOfRange,
                    std::move(ErrorPath)
                );
                return false;
            }

            Destination = static_cast<std::int64_t>(Value);
            return true;
        }

        if (!Field->is_number_integer())
        {
            Reject(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                std::move(ErrorPath)
            );
            return false;
        }

        Destination = Field->get<std::int64_t>();
        return true;
    }

    [[nodiscard]]
    bool DecodeCommonUser(
        const FJson& Data,
        FTSTikFinityDecodedUser& User,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        // Los campos son planos bajo data; el optional User no refleja data.user.
        return
            ReadOptionalString(
                Data, "uniqueId", "data.uniqueId", User.UniqueId, Result
            ) &&
            ReadOptionalString(
                Data, "nickname", "data.nickname", User.Nickname, Result
            ) &&
            ReadOptionalString(
                Data,
                "profilePictureUrl",
                "data.profilePictureUrl",
                User.ProfilePictureUrl,
                Result
            ) &&
            ReadOptionalInteger(
                Data, "followRole", "data.followRole", User.FollowRole, Result
            ) &&
            ReadOptionalBoolean(
                Data,
                "isModerator",
                "data.isModerator",
                User.bIsModerator,
                Result
            ) &&
            ReadOptionalBoolean(
                Data,
                "isSubscriber",
                "data.isSubscriber",
                User.bIsSubscriber,
                Result
            ) &&
            ReadOptionalBoolean(
                Data,
                "isNewGifter",
                "data.isNewGifter",
                User.bIsNewGifter,
                Result
            ) &&
            ReadOptionalInteger(
                Data,
                "topGifterRank",
                "data.topGifterRank",
                User.TopGifterRank,
                Result
            ) &&
            ReadOptionalInteger(
                Data,
                "gifterLevel",
                "data.gifterLevel",
                User.GifterLevel,
                Result
            ) &&
            ReadOptionalInteger(
                Data,
                "teamMemberLevel",
                "data.teamMemberLevel",
                User.TeamMemberLevel,
                Result
            );
    }

    [[nodiscard]]
    bool DecodeEmotes(
        const FJson& Data,
        std::vector<FTSTikFinityDecodedEmote>& Emotes,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const auto Field = Data.find("emotes");
        if (Field == Data.end())
        {
            return true;
        }
        if (!Field->is_array())
        {
            Reject(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                "data.emotes"
            );
            return false;
        }

        Emotes.reserve(Field->size());
        for (std::size_t Index = 0; Index < Field->size(); ++Index)
        {
            const FJson& Element = (*Field)[Index];
            const std::string ElementPath =
                "data.emotes[" + std::to_string(Index) + "]";
            if (!Element.is_object())
            {
                Reject(
                    Result,
                    ETSTikFinityJsonDecodeStatus::RejectedInvalidArrayElement,
                    ElementPath
                );
                return false;
            }

            FTSTikFinityDecodedEmote Emote;
            if (!ReadOptionalString(
                    Element,
                    "emoteId",
                    ElementPath + ".emoteId",
                    Emote.EmoteId,
                    Result
                ) ||
                !ReadOptionalString(
                    Element,
                    "emoteImageUrl",
                    ElementPath + ".emoteImageUrl",
                    Emote.EmoteImageUrl,
                    Result
                ))
            {
                return false;
            }

            Emotes.push_back(std::move(Emote));
        }
        return true;
    }

    [[nodiscard]]
    bool DecodeRoomUserViewerUser(
        const FJson& Object,
        std::string_view BasePath,
        FTSTikFinityDecodedRoomUserViewerUser& User,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const std::string Prefix(BasePath);
        return
            ReadOptionalString(
                Object,
                "uniqueId",
                Prefix + ".uniqueId",
                User.UniqueId,
                Result
            ) &&
            ReadOptionalString(
                Object,
                "nickname",
                Prefix + ".nickname",
                User.Nickname,
                Result
            ) &&
            ReadOptionalString(
                Object,
                "profilePictureUrl",
                Prefix + ".profilePictureUrl",
                User.ProfilePictureUrl,
                Result
            ) &&
            ReadOptionalBoolean(
                Object,
                "isModerator",
                Prefix + ".isModerator",
                User.bIsModerator,
                Result
            ) &&
            ReadOptionalBoolean(
                Object,
                "isSubscriber",
                Prefix + ".isSubscriber",
                User.bIsSubscriber,
                Result
            ) &&
            ReadOptionalInteger(
                Object,
                "gifterLevel",
                Prefix + ".gifterLevel",
                User.GifterLevel,
                Result
            ) &&
            ReadOptionalInteger(
                Object,
                "teamMemberLevel",
                Prefix + ".teamMemberLevel",
                User.TeamMemberLevel,
                Result
            );
    }

    [[nodiscard]]
    bool DecodeTopViewers(
        const FJson& Data,
        std::vector<FTSTikFinityDecodedTopViewer>& TopViewers,
        FTSTikFinityJsonDecodeResult& Result
    )
    {
        const auto Field = Data.find("topViewers");
        if (Field == Data.end())
        {
            return true;
        }
        if (!Field->is_array())
        {
            Reject(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                "data.topViewers"
            );
            return false;
        }

        TopViewers.reserve(Field->size());
        for (std::size_t Index = 0; Index < Field->size(); ++Index)
        {
            const FJson& Element = (*Field)[Index];
            const std::string ElementPath =
                "data.topViewers[" + std::to_string(Index) + "]";
            if (!Element.is_object())
            {
                Reject(
                    Result,
                    ETSTikFinityJsonDecodeStatus::RejectedInvalidArrayElement,
                    ElementPath
                );
                return false;
            }

            FTSTikFinityDecodedTopViewer TopViewer;
            if (!ReadOptionalInteger(
                    Element,
                    "coinCount",
                    ElementPath + ".coinCount",
                    TopViewer.CoinCount,
                    Result
                ))
            {
                return false;
            }

            const auto UserField = Element.find("user");
            if (UserField != Element.end())
            {
                if (!UserField->is_object())
                {
                    Reject(
                        Result,
                        ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                        ElementPath + ".user"
                    );
                    return false;
                }

                TopViewer.User.emplace();
                if (!DecodeRoomUserViewerUser(
                        *UserField,
                        ElementPath + ".user",
                        *TopViewer.User,
                        Result
                    ))
                {
                    return false;
                }
            }

            TopViewers.push_back(std::move(TopViewer));
        }
        return true;
    }

    template <typename TMessage>
    FTSTikFinityJsonDecodeResult FinishDecoded(
        FTSTikFinityJsonDecodeResult Result,
        TMessage Message
    )
    {
        Result.Status = ETSTikFinityJsonDecodeStatus::Decoded;
        Result.ErrorPath.clear();
        Result.Event = FTSTikFinityMappedEvent{std::move(Message)};
        return Result;
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeChat(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedChatMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        FTSTikFinityDecodedChatData& Data = *Message.Data;

        if (!ReadOptionalString(
                DataObject,
                "comment",
                "data.comment",
                Data.Comment,
                Result
            ) ||
            !DecodeEmotes(DataObject, Data.Emotes, Result))
        {
            return Result;
        }

        Data.User.emplace();
        if (!DecodeCommonUser(DataObject, *Data.User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeGift(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedGiftMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        FTSTikFinityDecodedGiftData& Data = *Message.Data;

        if (!ReadOptionalInteger(
                DataObject, "giftId", "data.giftId", Data.GiftId, Result
            ) ||
            !ReadOptionalString(
                DataObject, "giftName", "data.giftName", Data.GiftName, Result
            ) ||
            !ReadOptionalString(
                DataObject,
                "giftPictureUrl",
                "data.giftPictureUrl",
                Data.GiftPictureUrl,
                Result
            ) ||
            !ReadOptionalInteger(
                DataObject,
                "diamondCount",
                "data.diamondCount",
                Data.DiamondCount,
                Result
            ) ||
            !ReadOptionalInteger(
                DataObject,
                "repeatCount",
                "data.repeatCount",
                Data.RepeatCount,
                Result
            ) ||
            !ReadOptionalInteger(
                DataObject, "giftType", "data.giftType", Data.GiftType, Result
            ) ||
            !ReadOptionalString(
                DataObject, "describe", "data.describe", Data.Describe, Result
            ) ||
            !ReadOptionalBoolean(
                DataObject,
                "repeatEnd",
                "data.repeatEnd",
                Data.bRepeatEnd,
                Result
            ) ||
            !ReadOptionalString(
                DataObject, "groupId", "data.groupId", Data.GroupId, Result
            ))
        {
            return Result;
        }

        Data.User.emplace();
        if (!DecodeCommonUser(DataObject, *Data.User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeLike(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedLikeMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        FTSTikFinityDecodedLikeData& Data = *Message.Data;

        if (!ReadOptionalInteger(
                DataObject,
                "likeCount",
                "data.likeCount",
                Data.LikeCount,
                Result
            ) ||
            !ReadOptionalInteger(
                DataObject,
                "totalLikeCount",
                "data.totalLikeCount",
                Data.TotalLikeCount,
                Result
            ))
        {
            return Result;
        }

        Data.User.emplace();
        if (!DecodeCommonUser(DataObject, *Data.User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeFollow(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedFollowMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        Message.Data->User.emplace();
        if (!DecodeCommonUser(DataObject, *Message.Data->User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeShare(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedShareMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        Message.Data->User.emplace();
        if (!DecodeCommonUser(DataObject, *Message.Data->User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeMember(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedMemberMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        FTSTikFinityDecodedMemberData& Data = *Message.Data;

        if (!ReadOptionalInteger(
                DataObject, "actionId", "data.actionId", Data.ActionId, Result
            ))
        {
            return Result;
        }
        Data.User.emplace();
        if (!DecodeCommonUser(DataObject, *Data.User, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult DecodeRoomUser(
        const FJson& DataObject,
        FTSTikFinityJsonDecodeResult Result
    )
    {
        FTSTikFinityDecodedRoomUserMessage Message;
        Message.EventName = Result.EventName;
        Message.Data.emplace();
        FTSTikFinityDecodedRoomUserData& Data = *Message.Data;

        if (!ReadOptionalInteger(
                DataObject,
                "viewerCount",
                "data.viewerCount",
                Data.ViewerCount,
                Result
            ) ||
            !ReadOptionalInteger(
                DataObject,
                "topGifterRank",
                "data.topGifterRank",
                Data.TopGifterRank,
                Result
            ) ||
            !DecodeTopViewers(DataObject, Data.TopViewers, Result))
        {
            return Result;
        }
        return FinishDecoded(std::move(Result), std::move(Message));
    }
}

FTSTikFinityJsonDecodeResult FTSTikFinityJsonEventDecoder::Decode(
    std::string_view JsonText
)
{
    FTSTikFinityJsonDecodeResult Result;

    const bool bOnlyWhitespace = std::all_of(
        JsonText.begin(),
        JsonText.end(),
        [](char Character)
        {
            return std::isspace(static_cast<unsigned char>(Character)) != 0;
        }
    );
    if (JsonText.empty() || bOnlyWhitespace)
    {
        return Result;
    }

    const FJson Root = FJson::parse(
        JsonText.begin(), JsonText.end(), nullptr, false
    );
    if (Root.is_discarded())
    {
        Result.Status = ETSTikFinityJsonDecodeStatus::RejectedMalformedJson;
        return Result;
    }
    if (!Root.is_object())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedRootNotObject,
            "$"
        );
        return Result;
    }

    const auto EventField = Root.find("event");
    if (EventField == Root.end())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedMissingEvent,
            "event"
        );
        return Result;
    }
    if (!EventField->is_string())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName,
            "event"
        );
        return Result;
    }

    Result.EventName = EventField->get<std::string>();
    if (Result.EventName.empty())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName,
            "event"
        );
        return Result;
    }

    const std::optional<ETSTikFinityMappedEventKind> Kind =
        TryParseTikFinityMappedEventKind(Result.EventName);
    if (!Kind.has_value())
    {
        Result.Status = ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent;
        return Result;
    }

    const auto DataField = Root.find("data");
    if (DataField == Root.end())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedMissingData,
            "data"
        );
        return Result;
    }
    if (!DataField->is_object())
    {
        Reject(
            Result,
            ETSTikFinityJsonDecodeStatus::RejectedDataNotObject,
            "data"
        );
        return Result;
    }

    switch (*Kind)
    {
    case ETSTikFinityMappedEventKind::Chat:
        return DecodeChat(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Gift:
        return DecodeGift(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Like:
        return DecodeLike(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Follow:
        return DecodeFollow(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Share:
        return DecodeShare(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::RoomUser:
        return DecodeRoomUser(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Member:
        return DecodeMember(*DataField, std::move(Result));
    case ETSTikFinityMappedEventKind::Count:
        break;
    }

    throw std::logic_error("Invalid TikFinity mapped event kind");
}
