#pragma once

#include "TikFinity/TSTikFinityMappedEventContracts.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class ETSTikFinityJsonDecodeStatus : std::uint8_t
{
    Decoded,
    IgnoredUnknownEvent,
    RejectedEmptyFrame,
    RejectedMalformedJson,
    RejectedRootNotObject,
    RejectedMissingEvent,
    RejectedInvalidEventName,
    RejectedMissingData,
    RejectedDataNotObject,
    RejectedInvalidFieldType,
    RejectedNumericOutOfRange,
    RejectedInvalidArrayElement
};

struct FTSTikFinityJsonDecodeResult
{
    ETSTikFinityJsonDecodeStatus Status =
        ETSTikFinityJsonDecodeStatus::RejectedEmptyFrame;
    std::string EventName;

    // Primera ruta inválida según el orden estable del contrato decodificado.
    std::string ErrorPath;
    std::optional<FTSTikFinityMappedEvent> Event;
};

// Decoder sin estado; la dependencia JSON permanece confinada a su implementación.
class FTSTikFinityJsonEventDecoder final
{
public:
    [[nodiscard]]
    static FTSTikFinityJsonDecodeResult Decode(std::string_view JsonText);
};
