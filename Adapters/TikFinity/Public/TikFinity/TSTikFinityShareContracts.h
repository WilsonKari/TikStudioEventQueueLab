#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityShareConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonShareEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedInvalidNumericField
};

struct FTSTikFinityShareConversionResult
{
    ETSTikFinityShareConversionStatus Status =
        ETSTikFinityShareConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSShareInput> Input;
};
