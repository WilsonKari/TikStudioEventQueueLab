#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>

enum class ETSTikFinityMemberConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonMemberEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedMissingActionId,
    RejectedInvalidNumericField
};

struct FTSTikFinityMemberConversionResult
{
    ETSTikFinityMemberConversionStatus Status =
        ETSTikFinityMemberConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSMemberInput> Input;
};
