#pragma once

#include "EventPipeline/Payloads/TSRoomUserPayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSRoomUserProcessingDispatch =
    TTSProcessingDispatch<FTSRoomUserPayload>;
