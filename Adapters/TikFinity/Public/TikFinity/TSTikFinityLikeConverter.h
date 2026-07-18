#pragma once

#include "TikFinity/TSTikFinityLikeContracts.h"
#include "TikFinity/TSTikFinityMappedEventContracts.h"

// Conversión sin estado desde Like decodificado al contrato portable.
class FTSTikFinityLikeConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityLikeConversionResult Convert(
        const FTSTikFinityDecodedLikeMessage& Message
    );
};
