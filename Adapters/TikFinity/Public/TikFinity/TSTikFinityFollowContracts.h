#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityFollowConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonFollowEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedInvalidNumericField
};

struct FTSTikFinityFollowConversionResult
{
    ETSTikFinityFollowConversionStatus Status =
        ETSTikFinityFollowConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSFollowInput> Input;
};
