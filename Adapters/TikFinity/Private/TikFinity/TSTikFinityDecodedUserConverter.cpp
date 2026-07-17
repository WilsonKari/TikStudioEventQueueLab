#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include "TikFinity/TSTikFinityChatContracts.h"

#include <cstdint>
#include <limits>
#include <string>

namespace
{
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

std::optional<FTSUserSnapshot> FTSTikFinityDecodedUserConverter::TryConvert(
    const FTSTikFinityDecodedUser& User
)
{
    std::int32_t FollowRole = 0;
    std::int32_t TopGifterRank = 0;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;

    // Validar todos los destinos evita publicar un snapshot parcial.
    if (!TryConvertNumericField(User.FollowRole, FollowRole) ||
        !TryConvertNumericField(User.TopGifterRank, TopGifterRank) ||
        !TryConvertNumericField(User.GifterLevel, GifterLevel) ||
        !TryConvertNumericField(User.TeamMemberLevel, TeamMemberLevel))
    {
        return std::nullopt;
    }

    FTSUserSnapshot Snapshot;
    Snapshot.UniqueId = User.UniqueId.value_or(std::string{});
    Snapshot.Nickname = User.Nickname.value_or(std::string{});
    Snapshot.ProfilePictureUrl =
        User.ProfilePictureUrl.value_or(std::string{});
    Snapshot.FollowRole = FollowRole;
    Snapshot.bIsModerator = User.bIsModerator.value_or(false);
    Snapshot.bIsSubscriber = User.bIsSubscriber.value_or(false);
    Snapshot.bIsNewGifter = User.bIsNewGifter.value_or(false);
    Snapshot.TopGifterRank = TopGifterRank;
    Snapshot.GifterLevel = GifterLevel;
    Snapshot.TeamMemberLevel = TeamMemberLevel;
    return Snapshot;
}
