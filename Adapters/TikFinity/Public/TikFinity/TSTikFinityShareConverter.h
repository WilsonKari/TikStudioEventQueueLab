#pragma once

#include "TikFinity/TSTikFinityMappedEventContracts.h"
#include "TikFinity/TSTikFinityShareContracts.h"

// Conversión sin estado entre datos ya decodificados y el contrato portable Share.
class FTSTikFinityShareConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityShareConversionResult Convert(
        const FTSTikFinityDecodedShareMessage& Message
    );
};
