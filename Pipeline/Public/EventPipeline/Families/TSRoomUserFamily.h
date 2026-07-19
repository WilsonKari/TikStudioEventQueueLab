#pragma once

#include "EventPipeline/Payloads/TSRoomUserPayload.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

class FTSRoomUserFamily final
{
public:
    [[nodiscard]]
    static TTSFamilyDecision<FTSRoomUserPayload> Decide(
        FTSRoomUserInput Input
    );
};
