#pragma once

#include "TikFinity/TSTikFinityChatContracts.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct FTSTikFinityDecodedGiftData
{
    std::optional<std::int64_t> GiftId;
    std::optional<std::string> GiftName;
    std::optional<std::string> GiftPictureUrl;
    std::optional<std::int64_t> DiamondCount;
    std::optional<std::int64_t> RepeatCount;
    std::optional<std::int64_t> GiftType;
    std::optional<std::string> Describe;
    std::optional<bool> bRepeatEnd;
    std::optional<std::string> GroupId;
    std::optional<FTSTikFinityDecodedUser> User;
};

struct FTSTikFinityDecodedGiftMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedGiftData> Data;
};

struct FTSTikFinityDecodedLikeData
{
    std::optional<std::int64_t> LikeCount;
    std::optional<std::int64_t> TotalLikeCount;
    std::optional<FTSTikFinityDecodedUser> User;
};

struct FTSTikFinityDecodedLikeMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedLikeData> Data;
};

struct FTSTikFinityDecodedFollowData
{
    std::optional<FTSTikFinityDecodedUser> User;
};

struct FTSTikFinityDecodedFollowMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedFollowData> Data;
};

struct FTSTikFinityDecodedShareData
{
    std::optional<FTSTikFinityDecodedUser> User;
};

struct FTSTikFinityDecodedShareMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedShareData> Data;
};

struct FTSTikFinityDecodedMemberData
{
    std::optional<std::int64_t> ActionId;
    std::optional<FTSTikFinityDecodedUser> User;
};

struct FTSTikFinityDecodedMemberMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedMemberData> Data;
};

struct FTSTikFinityDecodedRoomUserViewerUser
{
    std::optional<std::string> UniqueId;
    std::optional<std::string> Nickname;
    std::optional<std::string> ProfilePictureUrl;
    std::optional<bool> bIsModerator;
    std::optional<bool> bIsSubscriber;
    std::optional<std::int64_t> GifterLevel;
    std::optional<std::int64_t> TeamMemberLevel;
};

struct FTSTikFinityDecodedTopViewer
{
    std::optional<std::int64_t> CoinCount;
    std::optional<FTSTikFinityDecodedRoomUserViewerUser> User;
};

struct FTSTikFinityDecodedRoomUserData
{
    std::optional<std::int64_t> ViewerCount;
    std::optional<std::int64_t> TopGifterRank;
    std::vector<FTSTikFinityDecodedTopViewer> TopViewers;
};

struct FTSTikFinityDecodedRoomUserMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedRoomUserData> Data;
};

enum class ETSTikFinityMappedEventKind : std::uint8_t
{
    Chat,
    Gift,
    Like,
    Follow,
    Share,
    RoomUser,
    Member,
    Count
};

inline constexpr std::size_t TSTikFinityMappedEventKindCount =
    static_cast<std::size_t>(ETSTikFinityMappedEventKind::Count);

// Unión cerrada exclusiva de la frontera TikFinity; nunca es un payload del core.
using FTSTikFinityMappedEvent = std::variant<
    FTSTikFinityDecodedChatMessage,
    FTSTikFinityDecodedGiftMessage,
    FTSTikFinityDecodedLikeMessage,
    FTSTikFinityDecodedFollowMessage,
    FTSTikFinityDecodedShareMessage,
    FTSTikFinityDecodedRoomUserMessage,
    FTSTikFinityDecodedMemberMessage
>;

[[nodiscard]]
inline std::optional<ETSTikFinityMappedEventKind>
TryParseTikFinityMappedEventKind(std::string_view EventName) noexcept
{
    if (EventName == "chat")
    {
        return ETSTikFinityMappedEventKind::Chat;
    }
    if (EventName == "gift")
    {
        return ETSTikFinityMappedEventKind::Gift;
    }
    if (EventName == "like")
    {
        return ETSTikFinityMappedEventKind::Like;
    }
    if (EventName == "follow")
    {
        return ETSTikFinityMappedEventKind::Follow;
    }
    if (EventName == "share")
    {
        return ETSTikFinityMappedEventKind::Share;
    }
    if (EventName == "roomUser")
    {
        return ETSTikFinityMappedEventKind::RoomUser;
    }
    if (EventName == "member")
    {
        return ETSTikFinityMappedEventKind::Member;
    }
    return std::nullopt;
}

[[nodiscard]]
inline std::string_view GetTikFinityMappedEventName(
    ETSTikFinityMappedEventKind Kind
)
{
    switch (Kind)
    {
    case ETSTikFinityMappedEventKind::Chat:
        return "chat";
    case ETSTikFinityMappedEventKind::Gift:
        return "gift";
    case ETSTikFinityMappedEventKind::Like:
        return "like";
    case ETSTikFinityMappedEventKind::Follow:
        return "follow";
    case ETSTikFinityMappedEventKind::Share:
        return "share";
    case ETSTikFinityMappedEventKind::RoomUser:
        return "roomUser";
    case ETSTikFinityMappedEventKind::Member:
        return "member";
    case ETSTikFinityMappedEventKind::Count:
        break;
    }

    throw std::logic_error("Invalid TikFinity mapped event kind");
}

[[nodiscard]]
inline ETSTikFinityMappedEventKind GetTikFinityMappedEventKind(
    const FTSTikFinityMappedEvent& Event
) noexcept
{
    static_assert(
        std::variant_size_v<FTSTikFinityMappedEvent> ==
        TSTikFinityMappedEventKindCount
    );

    // El orden de la variante forma parte del contrato y coincide con el enum.
    return static_cast<ETSTikFinityMappedEventKind>(Event.index());
}
