#pragma once

#include "EventPipeline/Payloads/TSShareMilestonePayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSShareMilestoneProcessingDispatch =
    TTSProcessingDispatch<FTSShareMilestonePayload>;
