#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

// Snapshot propio del input normalizado; no conserva referencias al productor.
struct FTSLikePayload
{
    FTSLikeInput Input{};
};
