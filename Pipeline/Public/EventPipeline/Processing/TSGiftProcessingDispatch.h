#pragma once

#include "EventPipeline/Payloads/TSGiftPayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSGiftProcessingDispatch =
    TTSProcessingDispatch<FTSGiftPayload>;
