#pragma once

#include "EventPipeline/Payloads/TSLikePayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSLikeProcessingDispatch =
    TTSProcessingDispatch<FTSLikePayload>;
