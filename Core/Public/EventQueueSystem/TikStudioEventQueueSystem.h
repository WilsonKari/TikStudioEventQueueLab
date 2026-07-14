#pragma once

#include "EventQueueSystem/TSEventQueueSystemOperations.h"
#include "EventQueueSystem/TSEventQueueSystemSettings.h"

class TikStudioEventQueueSystem final
{
public:
    explicit TikStudioEventQueueSystem(
        FTSEventQueueSettings Settings = FTSEventQueueSettings{}
    );

    [[nodiscard]]
    FTSEnqueueResult Enqueue(const FTSEnqueueRequest& Request);

    [[nodiscard]]
    FTSPumpResult Pump();

    [[nodiscard]]
    FTSConfirmResult Confirm(FTSEmissionId EmissionId);
};
