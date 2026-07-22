#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>

// Reglas semánticas compartidas por las familias de Pipeline; no forman parte de los
// settings genéricos ni de las políticas internas del Core.
struct FTSCommonUserPrioritySettings
{
    std::int64_t FollowRolePointsPerLevel = 5;
    std::int32_t MaxFollowRole = 2;

    std::int64_t ModeratorBonus = 15;
    std::int64_t SubscriberBonus = 10;
    std::int64_t NewGifterBonus = 15;

    std::int64_t TopGifterRankPointsPerPosition = 4;
    std::int32_t MaxRewardedTopGifterRank = 5;

    std::int64_t GifterLevelPointsPerLevel = 1;
    std::int32_t MaxGifterLevel = 50;

    std::int64_t TeamMemberLevelPointsPerLevel = 1;
    std::int32_t MaxTeamMemberLevel = 50;
};

// Conserva aportes diagnosticables y el total saturado que normalmente consumirán
// las familias.
struct FTSCommonUserPriorityBreakdown
{
    std::int64_t FollowRoleContribution = 0;
    std::int64_t ModeratorContribution = 0;
    std::int64_t SubscriberContribution = 0;
    std::int64_t NewGifterContribution = 0;
    std::int64_t TopGifterRankContribution = 0;
    std::int64_t GifterLevelContribution = 0;
    std::int64_t TeamMemberLevelContribution = 0;
    std::int64_t TotalAdjustment = 0;
};

class FTSCommonUserPriorityPolicy final
{
public:
    [[nodiscard]]
    static bool AreSettingsValid(
        const FTSCommonUserPrioritySettings& Settings
    ) noexcept;

    // Aplica los atributos comunes una sola vez por evaluación; no multiplica sus
    // aportes por mensajes, regalos, repeticiones ni otros elementos acumulados.
    [[nodiscard]]
    static FTSCommonUserPriorityBreakdown Evaluate(
        const FTSUserSnapshot& User,
        const FTSCommonUserPrioritySettings& Settings
    );
};
