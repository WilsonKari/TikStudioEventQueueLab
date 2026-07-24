#pragma once

#include "EventPipeline/Payloads/TSLikeMilestonePayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSLikeMilestoneFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSLikeMilestonePayload> Decide(
        FTSLikeInput Input
    );
};
