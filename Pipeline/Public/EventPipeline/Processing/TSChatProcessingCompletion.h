#pragma once

#include "EventPipeline/TikStudioEventPipelineContracts.h"
#include "EventQueueSystem/TSEventQueueSystemOperations.h"

#include <optional>

struct FTSChatProcessingCompletionResult
{
    FTSEmissionId EmissionId = 0;

    // Conserva el resultado comunicado por el procesador externo.
    ETSProcessingResult ProcessingResult =
        ETSProcessingResult::Failed;

    // Sólo existe para ProcessingResult::Succeeded.
    std::optional<FTSConfirmResult> ConfirmResult;

    // Existe para Cancelled y Failed.
    std::optional<FTSCancelInFlightResult> CancelResult;
};
