#pragma once

#include "TikFinity/TSTikFinityJsonEventDecoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

enum class ETSTikFinityChecklistObservationKind : std::uint8_t
{
    ValidKnownEvent,
    InvalidKnownEvent,
    UnknownEvent,
    InvalidFrame
};

struct FTSTikFinityEventChecklistEntry
{
    std::uint64_t ValidFrameCount = 0;
    std::uint64_t InvalidFrameCount = 0;
    bool bSeenValid = false;
};

struct FTSTikFinityChecklistObservation
{
    ETSTikFinityChecklistObservationKind ObservationKind =
        ETSTikFinityChecklistObservationKind::InvalidFrame;
    std::optional<ETSTikFinityMappedEventKind> EventKind;
    std::string EventName;
    bool bFirstValidObservation = false;
    std::size_t ValidEventKindsSeen = 0;
    bool bAllSevenSeenValid = false;
};

struct FTSTikFinitySevenEventChecklistSnapshot
{
    std::array<
        FTSTikFinityEventChecklistEntry,
        TSTikFinityMappedEventKindCount
    > Events{};

    std::uint64_t KnownDecodedEvents = 0;
    std::uint64_t UnknownEvents = 0;
    std::uint64_t InvalidFrames = 0;
    std::uint64_t TransportErrors = 0;
    std::uint64_t BinaryFrames = 0;

    [[nodiscard]]
    std::size_t GetValidEventKindsSeen() const noexcept;

    [[nodiscard]]
    bool HasSeenAllSevenValid() const noexcept;
};

// Autoridad de cobertura del probe; la sincronización pertenece al caller.
class FTSTikFinitySevenEventChecklist final
{
public:
    [[nodiscard]]
    FTSTikFinityChecklistObservation Observe(
        const FTSTikFinityJsonDecodeResult& Result
    );

    void RecordTransportError() noexcept;
    void RecordBinaryFrame() noexcept;

    [[nodiscard]]
    FTSTikFinitySevenEventChecklistSnapshot GetSnapshot() const;

private:
    FTSTikFinitySevenEventChecklistSnapshot Snapshot;
};
