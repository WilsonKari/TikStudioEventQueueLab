#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <optional>

struct FTSTikFinityDecodedUser;

// El caller valida la identidad; nullopt indica sólo un rango numérico inválido.
class FTSTikFinityDecodedUserConverter final
{
public:
    [[nodiscard]]
    static std::optional<FTSUserSnapshot> TryConvert(
        const FTSTikFinityDecodedUser& User
    );
};
