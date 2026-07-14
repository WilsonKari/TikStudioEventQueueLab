#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class ETSEventFlow : std::uint8_t
{
    Chat,
    Gift,
    GiftCombo,
    Follow,
    Like,
    LikeUser,
    MemberIdentity,
    MemberNormalized,
    RoomUser,
    RoomUserMilestone,
    RoomUserTop1Change,
    Share,
    ShareMilestone,
    Count
};

inline constexpr std::size_t TSEventFlowCount =
    static_cast<std::size_t>(ETSEventFlow::Count);

constexpr std::size_t ToIndex(ETSEventFlow Flow) noexcept
{
    return static_cast<std::size_t>(Flow);
}

[[nodiscard]]
constexpr bool IsValidFlow(ETSEventFlow Flow) noexcept
{
    return ToIndex(Flow) < TSEventFlowCount;
}

enum class ETSEventExpirePolicy : std::uint8_t
{
    Discard,
    Consolidate
};

// El reloj monotónico evita que cambios en la hora del sistema alteren el TTL.
using FTSEventQueueClock = std::chrono::steady_clock;
using FTSEventQueueTimePoint = FTSEventQueueClock::time_point;

// Produce el instante monotónico que el core capturará una vez por operación pública.
using FTSNowProvider = std::function<FTSEventQueueTimePoint()>;

using FTSEmissionId = std::uint64_t;
using FTSEmissionSequence = std::uint64_t;

// Metadatos portables que el core necesita para administrar una emisión.
struct FTSEmissionEnvelope
{
    FTSEmissionId EmissionId = 0;
    ETSEventFlow Flow = ETSEventFlow::Chat;
    FTSEmissionSequence Sequence = 0;
    FTSEventQueueTimePoint CreatedAt{};
    FTSEventQueueTimePoint ExpiresAt{};
    // Puntuación base congelada al admitir; el aging futuro se evaluará por separado.
    // Se calculará mediante una suma saturada de BaseWeight y PriorityAdjustment.
    std::int64_t PriorityScore = 0;
};

// Instantánea portable de los datos comunes del usuario recibidos con un evento.
struct FTSUserSnapshot
{
    std::string UniqueId;
    std::string Nickname;
    std::string ProfilePictureUrl;
    std::int32_t FollowRole = 0;
    bool bIsModerator = false;
    bool bIsSubscriber = false;
    bool bIsNewGifter = false;
    std::int32_t TopGifterRank = 0;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;
};

// Datos portables de un emote recibido con un evento Chat.
struct FTSEmoteInfo
{
    std::string EmoteId;
    std::string EmoteImageUrl;
};

struct FTSChatInput
{
    std::string Comment;
    std::vector<FTSEmoteInfo> Emotes;
    FTSUserSnapshot User;
};

struct FTSGiftInput
{
    std::int32_t GiftId = 0;
    std::string GiftName;
    std::string GiftPictureUrl;
    std::int32_t DiamondCount = 0;
    std::int32_t RepeatCount = 1;
    std::int32_t GiftType = 0;
    std::string Describe;
    bool bRepeatEnd = false;
    std::string GroupId;
    FTSUserSnapshot User;
};

struct FTSLikeInput
{
    std::int32_t LikeCount = 0;
    std::int32_t TotalLikeCount = 0;
    FTSUserSnapshot User;
};

struct FTSMemberInput
{
    std::int32_t ActionId = 0;
    FTSUserSnapshot User;
};

// Los top viewers tienen un contrato distinto al usuario común.
struct FTSRoomUserTopViewer
{
    std::string UniqueId;
    std::string Nickname;
    std::string ProfilePictureUrl;
    std::int32_t CoinCount = 0;
    bool bIsModerator = false;
    bool bIsSubscriber = false;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;
};

struct FTSRoomUserInput
{
    std::int32_t ViewerCount = 0;
    std::int32_t TopGifterRank = 0;
    std::vector<FTSRoomUserTopViewer> TopViewers;
};

struct FTSShareInput
{
    FTSUserSnapshot User;
};

struct FTSFollowInput
{
    FTSUserSnapshot User;
};
