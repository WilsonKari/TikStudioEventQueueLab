#pragma once

#include "EventPipeline/Processing/TSMemberPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSMemberFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSMemberPayload> Decide(FTSMemberInput Input);
};
