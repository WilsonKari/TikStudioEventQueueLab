#pragma once

#include "EventPipeline/Payloads/TSLikeMilestonePayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSLikeMilestoneProcessingDispatch =
    TTSProcessingDispatch<FTSLikeMilestonePayload>;
