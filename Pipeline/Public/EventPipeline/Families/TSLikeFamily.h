#pragma once

#include "EventPipeline/Payloads/TSLikePayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSLikeFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSLikePayload> Decide(FTSLikeInput Input);
};
