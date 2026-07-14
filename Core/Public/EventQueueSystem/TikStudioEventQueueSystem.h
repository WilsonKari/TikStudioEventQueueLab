#pragma once

#include "EventQueueSystem/TSEventQueueSystemOperations.h"
#include "EventQueueSystem/TSEventQueueSystemSettings.h"

#include <memory>

class TikStudioEventQueueSystem final
{
public:
    // Un proveedor vacío usará FTSEventQueueClock::now(); uno válido producirá
    // el instante monotónico que se capturará una vez por operación pública.
    explicit TikStudioEventQueueSystem(
        FTSEventQueueSettings Settings = FTSEventQueueSettings{},
        FTSNowProvider NowProvider = {}
    );

    ~TikStudioEventQueueSystem();

    TikStudioEventQueueSystem(
        const TikStudioEventQueueSystem&
    ) = delete;

    TikStudioEventQueueSystem& operator=(
        const TikStudioEventQueueSystem&
    ) = delete;

    TikStudioEventQueueSystem(
        TikStudioEventQueueSystem&&
    ) noexcept;

    TikStudioEventQueueSystem& operator=(
        TikStudioEventQueueSystem&&
    ) noexcept;

    [[nodiscard]]
    FTSEnqueueResult Enqueue(const FTSEnqueueRequest& Request);

    [[nodiscard]]
    FTSPumpResult Pump();

    [[nodiscard]]
    FTSConfirmResult Confirm(FTSEmissionId EmissionId);

    [[nodiscard]]
    FTSProcessDueExpirationsResult ProcessDueExpirations();

    [[nodiscard]]
    FTSNextWakeTimeResult GetNextWakeTime();

    [[nodiscard]]
    FTSCancelInFlightResult CancelInFlight(FTSEmissionId EmissionId);

private:
    class FImpl;

    std::unique_ptr<FImpl> Impl;
};
