#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <chrono>
#include <cstdint>

struct FTSEnqueueRequest
{
    ETSEventFlow Flow = ETSEventFlow::Chat;
    std::int64_t PriorityAdjustment = 0;
    bool bOverrideTTL = false;
    std::chrono::milliseconds TTLOverride{0};
    bool bProtectedFromEviction = false;
};

enum class ETSPumpStatus : std::uint8_t
{
    NotRequested,
    EmissionReady,
    QueueEmpty,
    Busy
};

struct FTSPumpResult
{
    ETSPumpStatus Status = ETSPumpStatus::NotRequested;
    FTSEmissionEnvelope ReadyEmission{};
};

enum class ETSEnqueueStatus : std::uint8_t
{
    Accepted,
    AcceptedWithEviction,
    RejectedInvalidFlow,
    RejectedDisabled,
    RejectedAtCapacity
};

struct FTSEnqueueResult
{
    ETSEnqueueStatus Status = ETSEnqueueStatus::RejectedInvalidFlow;
    FTSEmissionEnvelope AdmittedEmission{};
    FTSEmissionId EvictedEmissionId = 0;
    FTSPumpResult AutoPumpResult{};
};

enum class ETSConfirmStatus : std::uint8_t
{
    Confirmed,
    NoInFlightEmission,
    EmissionIdMismatch
};

struct FTSConfirmResult
{
    ETSConfirmStatus Status = ETSConfirmStatus::NoInFlightEmission;
    FTSEmissionId ConfirmedEmissionId = 0;
    FTSPumpResult AutoPumpResult{};
};
