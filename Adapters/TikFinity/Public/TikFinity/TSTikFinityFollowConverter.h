#pragma once

#include "TikFinity/TSTikFinityFollowContracts.h"
#include "TikFinity/TSTikFinityMappedEventContracts.h"

// Conversión sin estado entre datos ya decodificados y el contrato portable Follow.
class FTSTikFinityFollowConverter final
{
public:
    [[nodiscard]]
    static FTSTikFinityFollowConversionResult Convert(
        const FTSTikFinityDecodedFollowMessage& Message
    );
};
