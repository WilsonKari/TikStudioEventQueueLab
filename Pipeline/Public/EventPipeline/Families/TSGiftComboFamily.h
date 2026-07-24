#pragma once

#include "EventPipeline/Payloads/TSGiftComboPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSGiftComboFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSGiftComboPayload> Decide(
        FTSGiftInput Input
    );
};
