#pragma once

#include "EventPipeline/Payloads/TSMemberPayload.h"
#include "EventPipeline/Processing/TSProcessingDispatch.h"

using FTSMemberProcessingDispatch =
    TTSProcessingDispatch<FTSMemberPayload>;
