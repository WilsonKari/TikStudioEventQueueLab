#pragma once

#include "TikFinity/TSTikFinityMappedEventContracts.h"
#include "TikFinity/TSTikFinityRoomUserContracts.h"

// Conversión sin estado desde RoomUser decodificado al contrato portable.
class FTSTikFinityRoomUserConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityRoomUserConversionResult Convert(
        const FTSTikFinityDecodedRoomUserMessage& Message
    );
};
