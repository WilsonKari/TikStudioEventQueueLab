#include "TikFinity/TSTikFinitySevenEventChecklist.h"

#include <cstddef>
#include <stdexcept>

namespace
{
    [[nodiscard]]
    std::size_t ToIndex(ETSTikFinityMappedEventKind Kind) noexcept
    {
        return static_cast<std::size_t>(Kind);
    }
}

std::size_t
FTSTikFinitySevenEventChecklistSnapshot::GetValidEventKindsSeen() const noexcept
{
    std::size_t SeenCount = 0;
    for (const FTSTikFinityEventChecklistEntry& Entry : Events)
    {
        SeenCount += Entry.bSeenValid ? 1u : 0u;
    }
    return SeenCount;
}

bool FTSTikFinitySevenEventChecklistSnapshot::HasSeenAllSevenValid() const noexcept
{
    return GetValidEventKindsSeen() == TSTikFinityMappedEventKindCount;
}

FTSTikFinityChecklistObservation FTSTikFinitySevenEventChecklist::Observe(
    const FTSTikFinityJsonDecodeResult& Result
)
{
    FTSTikFinityChecklistObservation Observation;
    Observation.EventName = Result.EventName;

    if (Result.Status == ETSTikFinityJsonDecodeStatus::Decoded)
    {
        if (!Result.Event.has_value())
        {
            throw std::logic_error(
                "Decoded TikFinity result has no mapped event"
            );
        }

        const std::optional<ETSTikFinityMappedEventKind> NameKind =
            TryParseTikFinityMappedEventKind(Result.EventName);
        const ETSTikFinityMappedEventKind VariantKind =
            GetTikFinityMappedEventKind(*Result.Event);
        if (!NameKind.has_value() || *NameKind != VariantKind ||
            GetTikFinityMappedEventName(VariantKind) != Result.EventName)
        {
            throw std::logic_error(
                "TikFinity EventName and mapped variant do not match"
            );
        }

        FTSTikFinityEventChecklistEntry& Entry =
            Snapshot.Events[ToIndex(VariantKind)];
        ++Entry.ValidFrameCount;
        ++Snapshot.KnownDecodedEvents;
        Observation.bFirstValidObservation = !Entry.bSeenValid;
        Entry.bSeenValid = true;
        Observation.ObservationKind =
            ETSTikFinityChecklistObservationKind::ValidKnownEvent;
        Observation.EventKind = VariantKind;
    }
    else if (
        Result.Status == ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent
    )
    {
        ++Snapshot.UnknownEvents;
        Observation.ObservationKind =
            ETSTikFinityChecklistObservationKind::UnknownEvent;
    }
    else
    {
        const std::optional<ETSTikFinityMappedEventKind> KnownKind =
            TryParseTikFinityMappedEventKind(Result.EventName);
        ++Snapshot.InvalidFrames;

        if (KnownKind.has_value())
        {
            ++Snapshot.Events[ToIndex(*KnownKind)].InvalidFrameCount;
            Observation.ObservationKind =
                ETSTikFinityChecklistObservationKind::InvalidKnownEvent;
            Observation.EventKind = *KnownKind;
        }
        else
        {
            Observation.ObservationKind =
                ETSTikFinityChecklistObservationKind::InvalidFrame;
        }
    }

    Observation.ValidEventKindsSeen = Snapshot.GetValidEventKindsSeen();
    Observation.bAllSevenSeenValid = Snapshot.HasSeenAllSevenValid();
    return Observation;
}

void FTSTikFinitySevenEventChecklist::RecordTransportError() noexcept
{
    ++Snapshot.TransportErrors;
}

void FTSTikFinitySevenEventChecklist::RecordBinaryFrame() noexcept
{
    ++Snapshot.BinaryFrames;
}

FTSTikFinitySevenEventChecklistSnapshot
FTSTikFinitySevenEventChecklist::GetSnapshot() const
{
    return Snapshot;
}
