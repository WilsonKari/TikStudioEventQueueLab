#include "EventPipeline/Priority/TSCommonUserPriorityPolicy.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace
{
    constexpr std::int64_t MaxPriorityAdjustment =
        std::numeric_limits<std::int64_t>::max();

    [[nodiscard]]
    constexpr std::int32_t ClampToNonNegativeRange(
        std::int32_t Value,
        std::int32_t Maximum
    ) noexcept
    {
        if (Value <= 0 || Maximum == 0)
        {
            return 0;
        }

        return Value < Maximum ? Value : Maximum;
    }

    // La saturación protege a Pipeline frente a configuraciones futuras extremas sin
    // trasladar fórmulas de producto ni riesgo de overflow al Core.
    [[nodiscard]]
    constexpr std::int64_t SaturatingMultiplyNonNegative(
        std::int64_t Left,
        std::int64_t Right
    ) noexcept
    {
        if (Left == 0 || Right == 0)
        {
            return 0;
        }

        if (Left > MaxPriorityAdjustment / Right)
        {
            return MaxPriorityAdjustment;
        }

        return Left * Right;
    }

    [[nodiscard]]
    constexpr std::int64_t SaturatingAddNonNegative(
        std::int64_t Left,
        std::int64_t Right
    ) noexcept
    {
        if (Left > MaxPriorityAdjustment - Right)
        {
            return MaxPriorityAdjustment;
        }

        return Left + Right;
    }
}

bool FTSCommonUserPriorityPolicy::AreSettingsValid(
    const FTSCommonUserPrioritySettings& Settings
) noexcept
{
    return Settings.FollowRolePointsPerLevel >= 0
        && Settings.MaxFollowRole >= 0
        && Settings.ModeratorBonus >= 0
        && Settings.SubscriberBonus >= 0
        && Settings.NewGifterBonus >= 0
        && Settings.TopGifterRankPointsPerPosition >= 0
        && Settings.MaxRewardedTopGifterRank >= 0
        && Settings.GifterLevelPointsPerLevel >= 0
        && Settings.MaxGifterLevel >= 0
        && Settings.TeamMemberLevelPointsPerLevel >= 0
        && Settings.MaxTeamMemberLevel >= 0;
}

FTSCommonUserPriorityBreakdown FTSCommonUserPriorityPolicy::Evaluate(
    const FTSUserSnapshot& User,
    const FTSCommonUserPrioritySettings& Settings
)
{
    if (!AreSettingsValid(Settings))
    {
        throw std::invalid_argument(
            "Common user priority settings cannot contain negative values"
        );
    }

    FTSCommonUserPriorityBreakdown Breakdown;

    const std::int32_t FollowRole = ClampToNonNegativeRange(
        User.FollowRole,
        Settings.MaxFollowRole
    );
    Breakdown.FollowRoleContribution = SaturatingMultiplyNonNegative(
        static_cast<std::int64_t>(FollowRole),
        Settings.FollowRolePointsPerLevel
    );

    Breakdown.ModeratorContribution = User.bIsModerator
        ? Settings.ModeratorBonus
        : 0;
    Breakdown.SubscriberContribution = User.bIsSubscriber
        ? Settings.SubscriberBonus
        : 0;
    Breakdown.NewGifterContribution = User.bIsNewGifter
        ? Settings.NewGifterBonus
        : 0;

    if (
        User.TopGifterRank >= 1
        && User.TopGifterRank <= Settings.MaxRewardedTopGifterRank
    )
    {
        const std::int64_t RewardedPositionCount =
            static_cast<std::int64_t>(Settings.MaxRewardedTopGifterRank)
            - static_cast<std::int64_t>(User.TopGifterRank)
            + 1;
        Breakdown.TopGifterRankContribution =
            SaturatingMultiplyNonNegative(
                RewardedPositionCount,
                Settings.TopGifterRankPointsPerPosition
            );
    }

    const std::int32_t GifterLevel = ClampToNonNegativeRange(
        User.GifterLevel,
        Settings.MaxGifterLevel
    );
    Breakdown.GifterLevelContribution = SaturatingMultiplyNonNegative(
        static_cast<std::int64_t>(GifterLevel),
        Settings.GifterLevelPointsPerLevel
    );

    const std::int32_t TeamMemberLevel = ClampToNonNegativeRange(
        User.TeamMemberLevel,
        Settings.MaxTeamMemberLevel
    );
    Breakdown.TeamMemberLevelContribution = SaturatingMultiplyNonNegative(
        static_cast<std::int64_t>(TeamMemberLevel),
        Settings.TeamMemberLevelPointsPerLevel
    );

    Breakdown.TotalAdjustment = Breakdown.FollowRoleContribution;
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.ModeratorContribution
    );
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.SubscriberContribution
    );
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.NewGifterContribution
    );
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.TopGifterRankContribution
    );
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.GifterLevelContribution
    );
    Breakdown.TotalAdjustment = SaturatingAddNonNegative(
        Breakdown.TotalAdjustment,
        Breakdown.TeamMemberLevelContribution
    );

    return Breakdown;
}
