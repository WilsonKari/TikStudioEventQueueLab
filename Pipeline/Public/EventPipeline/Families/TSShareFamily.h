#pragma once

#include "EventPipeline/Payloads/TSSharePayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSShareFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSSharePayload> Decide(FTSShareInput Input);
};
