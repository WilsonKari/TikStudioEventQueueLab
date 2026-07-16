#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

// Snapshot propio del input normalizado; no almacena referencias al productor.
struct FTSChatPayload
{
    FTSChatInput Input{};
};
