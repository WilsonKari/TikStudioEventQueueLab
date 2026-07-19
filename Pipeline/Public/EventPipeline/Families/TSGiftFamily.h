#pragma once

#include "EventPipeline/Payloads/TSGiftPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSGiftFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSGiftPayload> Decide(FTSGiftInput Input);
};
