#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityRoomUserConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonRoomUserEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingViewerCount,
    RejectedMissingTopViewerCoinCount,
    RejectedMissingTopViewerUser,
    RejectedMissingTopViewerIdentity,
    RejectedInvalidNumericField
};

struct FTSTikFinityRoomUserConversionResult
{
    ETSTikFinityRoomUserConversionStatus Status =
        ETSTikFinityRoomUserConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSRoomUserInput> Input;
};
