#pragma once

#include "EventPipeline/Payloads/TSShareMilestonePayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSShareMilestoneFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSShareMilestonePayload> Decide(
        FTSShareInput Input
    );
};
