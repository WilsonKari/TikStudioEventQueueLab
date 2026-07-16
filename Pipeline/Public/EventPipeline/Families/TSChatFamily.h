#pragma once

#include "EventPipeline/Payloads/TSChatPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSChatFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSChatPayload> Decide(FTSChatInput Input);
};
