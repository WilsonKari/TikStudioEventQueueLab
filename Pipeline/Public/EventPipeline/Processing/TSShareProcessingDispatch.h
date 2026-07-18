#pragma once

#include "EventPipeline/Payloads/TSSharePayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSShareProcessingDispatch =
    TTSProcessingDispatch<FTSSharePayload>;
