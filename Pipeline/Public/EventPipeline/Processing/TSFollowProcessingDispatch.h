#pragma once

#include "EventPipeline/Payloads/TSFollowPayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSFollowProcessingDispatch =
    TTSProcessingDispatch<FTSFollowPayload>;
