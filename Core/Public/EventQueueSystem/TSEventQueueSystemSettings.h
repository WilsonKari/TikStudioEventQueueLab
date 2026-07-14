#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <array>
#include <chrono>
#include <cstdint>

struct FTSFlowQueueSettings
{
    bool bEnabled = true;
    std::int32_t BaseWeight = 0;
    std::chrono::milliseconds TTL{0};
    ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard;
    std::uint32_t MaxSlots = 0;
    bool bExemptFromEviction = false;
};

struct FTSEvictionSettings
{
    bool bEnableCompetitiveEviction = false;
    bool bTrackEvictionMetrics = false;
};

struct FTSFairnessSettings
{
    double AgingPointsPerSecond = 0.0;
    std::int32_t AgingMaxBonus = 20;
};

struct FTSPumpBehaviorSettings
{
    bool bPumpOnFirstEnqueue = true;
    bool bPumpAfterConfirm = true;
    bool bRecomputeOnPump = true;
};

struct FTSEventQueueSettings
{
    std::array<FTSFlowQueueSettings, TSEventFlowCount> Flows;
    FTSEvictionSettings Eviction;
    FTSFairnessSettings Fairness;
    FTSPumpBehaviorSettings Pump;

    FTSEventQueueSettings();

    FTSFlowQueueSettings& GetFlowSettings(ETSEventFlow Flow) noexcept
    {
        return Flows[ToIndex(Flow)];
    }

    const FTSFlowQueueSettings& GetFlowSettings(ETSEventFlow Flow) const noexcept
    {
        return Flows[ToIndex(Flow)];
    }
};
