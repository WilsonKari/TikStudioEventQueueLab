#pragma once

#include "TikFinity/TSTikFinityChatContracts.h"

// Conversión sin estado entre datos ya decodificados y el contrato portable Chat.
class FTSTikFinityChatConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityChatConversionResult Convert(
        const FTSTikFinityDecodedChatMessage& Message
    );
};
