#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityGiftConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonGiftEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedMissingGiftId,
    RejectedMissingGiftName,
    RejectedMissingDiamondCount,
    RejectedInvalidNumericField
};

struct FTSTikFinityGiftConversionResult
{
    ETSTikFinityGiftConversionStatus Status =
        ETSTikFinityGiftConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSGiftInput> Input;
};
