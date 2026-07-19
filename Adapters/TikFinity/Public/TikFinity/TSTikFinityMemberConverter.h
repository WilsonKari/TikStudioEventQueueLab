#pragma once

#include "TikFinity/TSTikFinityMappedEventContracts.h"
#include "TikFinity/TSTikFinityMemberContracts.h"

// Conversión sin estado desde Member decodificado al contrato portable.
class FTSTikFinityMemberConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityMemberConversionResult Convert(
        const FTSTikFinityDecodedMemberMessage& Message
    );
};
