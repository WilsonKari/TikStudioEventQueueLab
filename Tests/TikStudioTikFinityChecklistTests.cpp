#include "TikFinity/TSTikFinitySevenEventChecklist.h"

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void Require(bool bCondition, const std::string& Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    [[nodiscard]]
    std::size_t ToIndex(ETSTikFinityMappedEventKind Kind)
    {
        return static_cast<std::size_t>(Kind);
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult MakeDecoded(
        ETSTikFinityMappedEventKind Kind
    )
    {
        FTSTikFinityJsonDecodeResult Result;
        Result.Status = ETSTikFinityJsonDecodeStatus::Decoded;
        Result.EventName = std::string(GetTikFinityMappedEventName(Kind));

        switch (Kind)
        {
        case ETSTikFinityMappedEventKind::Chat:
            Result.Event = FTSTikFinityDecodedChatMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Gift:
            Result.Event = FTSTikFinityDecodedGiftMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Like:
            Result.Event = FTSTikFinityDecodedLikeMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Follow:
            Result.Event = FTSTikFinityDecodedFollowMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Share:
            Result.Event = FTSTikFinityDecodedShareMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::RoomUser:
            Result.Event = FTSTikFinityDecodedRoomUserMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Member:
            Result.Event = FTSTikFinityDecodedMemberMessage{Result.EventName, {}};
            break;
        case ETSTikFinityMappedEventKind::Count:
            throw std::logic_error("Count is not an event kind");
        }
        return Result;
    }

    [[nodiscard]]
    FTSTikFinityJsonDecodeResult MakeInvalidKnown(std::string EventName)
    {
        FTSTikFinityJsonDecodeResult Result;
        Result.Status = ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType;
        Result.EventName = std::move(EventName);
        Result.ErrorPath = "data.field";
        return Result;
    }

    void TestInitialState()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        const auto Snapshot = Checklist.GetSnapshot();
        Require(Snapshot.GetValidEventKindsSeen() == 0, "Initial progress");
        Require(!Snapshot.HasSeenAllSevenValid(), "Initial all-seven state");
        Require(Snapshot.KnownDecodedEvents == 0, "Initial known count");
        Require(Snapshot.UnknownEvents == 0, "Initial unknown count");
        Require(Snapshot.InvalidFrames == 0, "Initial invalid count");
        Require(Snapshot.TransportErrors == 0, "Initial transport count");
        Require(Snapshot.BinaryFrames == 0, "Initial binary count");
        for (const auto& Entry : Snapshot.Events)
        {
            Require(
                Entry.ValidFrameCount == 0 && Entry.InvalidFrameCount == 0 &&
                    !Entry.bSeenValid,
                "Initial event entry"
            );
        }
    }

    void TestFirstValidEvent()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        const auto Observation = Checklist.Observe(
            MakeDecoded(ETSTikFinityMappedEventKind::Chat)
        );
        const auto Snapshot = Checklist.GetSnapshot();
        const auto& Chat = Snapshot.Events[ToIndex(ETSTikFinityMappedEventKind::Chat)];
        Require(
            Observation.ObservationKind ==
                ETSTikFinityChecklistObservationKind::ValidKnownEvent,
            "First Chat observation kind"
        );
        Require(Observation.EventKind == ETSTikFinityMappedEventKind::Chat, "First Chat kind");
        Require(Observation.bFirstValidObservation, "First Chat first flag");
        Require(Observation.ValidEventKindsSeen == 1, "First Chat progress");
        Require(Chat.ValidFrameCount == 1 && Chat.bSeenValid, "First Chat entry");
    }

    void TestRepeatedValidEvent()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        (void)Checklist.Observe(MakeDecoded(ETSTikFinityMappedEventKind::Chat));
        const auto Observation = Checklist.Observe(
            MakeDecoded(ETSTikFinityMappedEventKind::Chat)
        );
        const auto Snapshot = Checklist.GetSnapshot();
        const auto& Chat = Snapshot.Events[ToIndex(ETSTikFinityMappedEventKind::Chat)];
        Require(Chat.ValidFrameCount == 2, "Repeated Chat count");
        Require(Observation.ValidEventKindsSeen == 1, "Repeated Chat progress");
        Require(!Observation.bFirstValidObservation, "Repeated Chat first flag");
    }

    void TestInvalidKnownEvent()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        const auto Observation = Checklist.Observe(MakeInvalidKnown("gift"));
        const auto Snapshot = Checklist.GetSnapshot();
        const auto& Gift = Snapshot.Events[ToIndex(ETSTikFinityMappedEventKind::Gift)];
        Require(
            Observation.ObservationKind ==
                ETSTikFinityChecklistObservationKind::InvalidKnownEvent,
            "Invalid Gift observation kind"
        );
        Require(Observation.EventKind == ETSTikFinityMappedEventKind::Gift, "Invalid Gift kind");
        Require(Gift.InvalidFrameCount == 1 && !Gift.bSeenValid, "Invalid Gift entry");
        Require(Snapshot.InvalidFrames == 1, "Invalid Gift global count");
        Require(Snapshot.GetValidEventKindsSeen() == 0, "Invalid Gift progress");
    }

    void TestUnknownEvent()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        FTSTikFinityJsonDecodeResult Result;
        Result.Status = ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent;
        Result.EventName = "subscribe";
        const auto Observation = Checklist.Observe(Result);
        const auto Snapshot = Checklist.GetSnapshot();
        Require(
            Observation.ObservationKind ==
                ETSTikFinityChecklistObservationKind::UnknownEvent,
            "Unknown observation kind"
        );
        Require(Snapshot.UnknownEvents == 1, "Unknown count");
        Require(Snapshot.GetValidEventKindsSeen() == 0, "Unknown progress");
    }

    void TestInvalidFrameWithoutEvent()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        FTSTikFinityJsonDecodeResult Result;
        Result.Status = ETSTikFinityJsonDecodeStatus::RejectedMalformedJson;
        const auto Observation = Checklist.Observe(Result);
        const auto Snapshot = Checklist.GetSnapshot();
        Require(
            Observation.ObservationKind ==
                ETSTikFinityChecklistObservationKind::InvalidFrame,
            "Invalid frame observation kind"
        );
        Require(Snapshot.InvalidFrames == 1, "Invalid frame count");
        Require(Snapshot.GetValidEventKindsSeen() == 0, "Invalid frame progress");
    }

    void TestExternalCounters()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        Checklist.RecordTransportError();
        Checklist.RecordTransportError();
        Checklist.RecordBinaryFrame();
        const auto Snapshot = Checklist.GetSnapshot();
        Require(Snapshot.TransportErrors == 2, "Transport error count");
        Require(Snapshot.BinaryFrames == 1, "Binary frame count");
    }

    void TestAllSevenInArbitraryOrder()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        constexpr std::array<ETSTikFinityMappedEventKind, 7> Order{
            ETSTikFinityMappedEventKind::RoomUser,
            ETSTikFinityMappedEventKind::Chat,
            ETSTikFinityMappedEventKind::Member,
            ETSTikFinityMappedEventKind::Gift,
            ETSTikFinityMappedEventKind::Share,
            ETSTikFinityMappedEventKind::Like,
            ETSTikFinityMappedEventKind::Follow
        };

        for (std::size_t Index = 0; Index < Order.size(); ++Index)
        {
            const auto Observation = Checklist.Observe(MakeDecoded(Order[Index]));
            Require(
                Observation.ValidEventKindsSeen == Index + 1,
                "All-seven monotonic step"
            );
            Require(
                Observation.bAllSevenSeenValid == (Index + 1 == Order.size()),
                "All-seven completion flag"
            );
        }
        Require(Checklist.GetSnapshot().HasSeenAllSevenValid(), "All seven seen");
    }

    void TestInvalidBeforeValid()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        (void)Checklist.Observe(MakeInvalidKnown("gift"));
        const auto Observation = Checklist.Observe(
            MakeDecoded(ETSTikFinityMappedEventKind::Gift)
        );
        const auto& Gift = Checklist.GetSnapshot().Events[
            ToIndex(ETSTikFinityMappedEventKind::Gift)
        ];
        Require(Gift.InvalidFrameCount == 1, "Gift invalid-before-valid invalid count");
        Require(Gift.ValidFrameCount == 1, "Gift invalid-before-valid valid count");
        Require(Gift.bSeenValid, "Gift invalid-before-valid seen");
        Require(Observation.bFirstValidObservation, "Gift first valid flag");
    }

    void TestProgressNeverRegresses()
    {
        FTSTikFinitySevenEventChecklist Checklist;
        for (std::size_t Index = 0;
             Index < TSTikFinityMappedEventKindCount;
             ++Index)
        {
            (void)Checklist.Observe(MakeDecoded(
                static_cast<ETSTikFinityMappedEventKind>(Index)
            ));
        }

        (void)Checklist.Observe(MakeDecoded(ETSTikFinityMappedEventKind::Chat));
        FTSTikFinityJsonDecodeResult Unknown;
        Unknown.Status = ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent;
        Unknown.EventName = "unknown";
        (void)Checklist.Observe(Unknown);
        (void)Checklist.Observe(MakeInvalidKnown("gift"));

        const auto Snapshot = Checklist.GetSnapshot();
        Require(Snapshot.GetValidEventKindsSeen() == 7, "Final progress");
        Require(Snapshot.HasSeenAllSevenValid(), "Final all-seven state");
    }

    using FTestFunction = void (*)();

    struct FTestCase
    {
        const char* Name = nullptr;
        FTestFunction Function = nullptr;
    };
}

int main()
{
    const std::vector<FTestCase> Tests{
        {"Initial state", &TestInitialState},
        {"First valid event", &TestFirstValidEvent},
        {"Repeated valid event", &TestRepeatedValidEvent},
        {"Invalid known event", &TestInvalidKnownEvent},
        {"Unknown event", &TestUnknownEvent},
        {"Invalid frame without event", &TestInvalidFrameWithoutEvent},
        {"External counters", &TestExternalCounters},
        {"All seven in arbitrary order", &TestAllSevenInArbitraryOrder},
        {"Invalid before valid", &TestInvalidBeforeValid},
        {"Progress never regresses", &TestProgressNeverRegresses}
    };

    std::size_t PassedCount = 0;
    std::size_t FailedCount = 0;
    for (const FTestCase& Test : Tests)
    {
        try
        {
            Test.Function();
            ++PassedCount;
            std::cout << "PASS: " << Test.Name << '\n';
        }
        catch (const std::exception& Error)
        {
            ++FailedCount;
            std::cerr << "FAIL: " << Test.Name << " - " << Error.what() << '\n';
        }
        catch (...)
        {
            ++FailedCount;
            std::cerr << "FAIL: " << Test.Name << " - unknown exception\n";
        }
    }

    std::cout << "RESULT: " << PassedCount << " passed, "
              << FailedCount << " failed\n";
    return FailedCount == 0 ? 0 : 1;
}
