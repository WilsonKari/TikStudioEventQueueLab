#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

template <typename TPayload>
struct TTSProcessingDispatch
{
    FTSEmissionEnvelope Emission{};
    TPayload Payload{};
};
