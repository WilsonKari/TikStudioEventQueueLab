#pragma once

#include "EventPipeline/Payloads/TSFollowPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSFollowFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSFollowPayload> Decide(FTSFollowInput Input);
};
