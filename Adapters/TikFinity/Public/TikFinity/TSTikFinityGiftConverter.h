#pragma once

#include "TikFinity/TSTikFinityGiftContracts.h"
#include "TikFinity/TSTikFinityMappedEventContracts.h"

// Conversión sin estado desde Gift decodificado al contrato portable.
class FTSTikFinityGiftConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityGiftConversionResult Convert(
        const FTSTikFinityDecodedGiftMessage& Message
    );
};
