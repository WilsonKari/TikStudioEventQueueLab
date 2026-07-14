#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <array>
#include <chrono>
#include <cstdint>

struct FTSFlowQueueSettings
{
    bool bEnabled = true;
    std::int32_t BaseWeight = 0;
    // TTL > 0 expira mientras la emisión permanezca Pending; TTL == 0 no expira;
    // TTL < 0 es inválido. Sin expiración se representará con
    // FTSEventQueueTimePoint::max() y sin una entrada en el futuro índice.
    std::chrono::milliseconds TTL{0};
    ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard;
    // Cuenta emisiones vivas Pending + InFlight. Cero significa sin capacidad.
    // Pump no libera un slot; Confirm, Cancel, expiración y evicción sí lo liberan.
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
    // Tras un Enqueue aceptado, intenta Pump si no existe una emisión InFlight.
    // No significa primera emisión histórica, transición desde vacío ni temporizador.
    bool bPumpAfterEnqueueWhenIdle = true;
    // Tras Confirm exitoso, intenta Pump cuando el core queda sin InFlight.
    bool bPumpAfterConfirm = true;
};

// Cada emisión admitida conserva snapshots efectivos de prioridad, TTL, política de
// expiración y protección. Una futura actualización explícita podrá afectar emisiones
// nuevas, pero no modificará silenciosamente emisiones existentes.
struct FTSEventQueueSettings
{
    std::array<FTSFlowQueueSettings, TSEventFlowCount> Flows;
    FTSEvictionSettings Eviction;
    FTSFairnessSettings Fairness;
    FTSPumpBehaviorSettings Pump;

    FTSEventQueueSettings();

    [[nodiscard]]
    FTSFlowQueueSettings* TryGetFlowSettings(ETSEventFlow Flow) noexcept
    {
        return IsValidFlow(Flow) ? &Flows[ToIndex(Flow)] : nullptr;
    }

    [[nodiscard]]
    const FTSFlowQueueSettings* TryGetFlowSettings(ETSEventFlow Flow) const noexcept
    {
        return IsValidFlow(Flow) ? &Flows[ToIndex(Flow)] : nullptr;
    }
};
