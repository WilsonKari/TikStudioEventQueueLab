#pragma once

#include "EventPipeline/Priority/TSCommonUserPriorityPolicy.h"
#include "EventPipeline/Settings/TSChatSemanticSettings.h"

// Agrupa únicamente la semántica que ya pertenece al Pipeline.
struct FTSEventPipelineSettings
{
    FTSCommonUserPrioritySettings CommonUserPriority;
    FTSChatSemanticSettings Chat;
};
