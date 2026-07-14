#pragma once

#include "EventQueueSystem/TSEventQueueSystemOperations.h"
#include "EventQueueSystem/TSEventQueueSystemSettings.h"

class TikStudioEventQueueSystem final
{
public:
    // Un proveedor vacío usará FTSEventQueueClock::now(); uno válido producirá
    // el instante monotónico que se capturará una vez por operación pública.
    explicit TikStudioEventQueueSystem(
        FTSEventQueueSettings Settings = FTSEventQueueSettings{},
        FTSNowProvider NowProvider = {}
    );

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
};
