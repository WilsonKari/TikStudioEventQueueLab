#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <string>
#include <vector>

struct FTSChatMessageEntry
{
    std::string Comment;
    std::vector<FTSEmoteInfo> Emotes;
    FTSEventQueueTimePoint ReceivedAt{};
    bool bIsCommand = false;
};

// Un lote conserva intervenciones separadas y el snapshot más reciente del usuario.
struct FTSChatPayload
{
    FTSUserSnapshot User;
    std::vector<FTSChatMessageEntry> Messages;
    // Se evalúa una sola vez al crear el lote y permanece congelado al acumular.
    std::int64_t CommonPriorityAdjustment = 0;
};
