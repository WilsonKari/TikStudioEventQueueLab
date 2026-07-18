#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityLikeConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonLikeEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedMissingLikeCount,
    RejectedMissingTotalLikeCount,
    RejectedInvalidNumericField
};

struct FTSTikFinityLikeConversionResult
{
    ETSTikFinityLikeConversionStatus Status =
        ETSTikFinityLikeConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSLikeInput> Input;
};
