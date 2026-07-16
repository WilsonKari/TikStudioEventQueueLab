#pragma once

#include "EventPipeline/Payloads/TSChatPayload.h"
#include "EventQueueSystem/TSEventQueueSystemTypes.h"

struct FTSChatProcessingDispatch
{
    FTSEmissionEnvelope Emission{};
    FTSChatPayload Payload{};
};
