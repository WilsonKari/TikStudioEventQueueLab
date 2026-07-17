#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityMappedEventFormatter.h"
#include "TikFinity/TSTikFinitySevenEventChecklist.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace
{
    constexpr std::string_view DefaultUrl = "ws://127.0.0.1:21213";
    constexpr std::uint32_t DefaultDurationSeconds = 120;
    constexpr std::uint32_t MaximumDurationSeconds = 86400;
    constexpr std::size_t MaximumRawFrameCharacters = 512;

    struct FProbeOptions
    {
        std::string Url{DefaultUrl};
        std::uint32_t DurationSeconds = DefaultDurationSeconds;
        bool bStopWhenAllSeen = false;
        bool bRequireAllSeen = false;
        bool bVerbose = false;
    };

    struct FProbeState
    {
        std::mutex ChecklistMutex;
        std::mutex OutputMutex;
        FTSTikFinitySevenEventChecklist Checklist;
        std::atomic<bool> bStopRequested{false};
        std::atomic<bool> bInternalFailure{false};
    };

    class FNetworkSystemGuard final
    {
    public:
        FNetworkSystemGuard()
        {
            if (!ix::initNetSystem())
            {
                throw std::runtime_error("IXWebSocket network initialization failed");
            }
        }

        ~FNetworkSystemGuard()
        {
            (void)ix::uninitNetSystem();
        }

        FNetworkSystemGuard(const FNetworkSystemGuard&) = delete;
        FNetworkSystemGuard& operator=(const FNetworkSystemGuard&) = delete;
    };

    [[nodiscard]]
    std::optional<std::uint32_t> ParsePositiveDuration(std::string_view Text)
    {
        std::uint32_t Value = 0;
        const char* Begin = Text.data();
        const char* End = Text.data() + Text.size();
        const auto [Next, Error] = std::from_chars(Begin, End, Value);
        if (Error != std::errc{} || Next != End || Value == 0 ||
            Value > MaximumDurationSeconds)
        {
            return std::nullopt;
        }
        return Value;
    }

    [[nodiscard]]
    FProbeOptions ParseArguments(int ArgumentCount, char* Arguments[])
    {
        FProbeOptions Options;
        for (int Index = 1; Index < ArgumentCount; ++Index)
        {
            const std::string_view Argument(Arguments[Index]);
            if (Argument == "--url")
            {
                if (++Index >= ArgumentCount || Arguments[Index][0] == '\0' ||
                    std::string_view(Arguments[Index]).starts_with("--"))
                {
                    throw std::invalid_argument("--url requires a non-empty value");
                }
                Options.Url = Arguments[Index];
            }
            else if (Argument == "--seconds")
            {
                if (++Index >= ArgumentCount)
                {
                    throw std::invalid_argument("--seconds requires a value");
                }
                const auto Duration = ParsePositiveDuration(Arguments[Index]);
                if (!Duration.has_value())
                {
                    throw std::invalid_argument(
                        "--seconds must be an integer from 1 through 86400"
                    );
                }
                Options.DurationSeconds = *Duration;
            }
            else if (Argument == "--stop-when-all-seen")
            {
                Options.bStopWhenAllSeen = true;
            }
            else if (Argument == "--require-all-seen")
            {
                Options.bRequireAllSeen = true;
            }
            else if (Argument == "--verbose")
            {
                Options.bVerbose = true;
            }
            else
            {
                throw std::invalid_argument(
                    "unknown argument: " + std::string(Argument)
                );
            }
        }
        return Options;
    }

    [[nodiscard]]
    std::string_view DecodeStatusName(ETSTikFinityJsonDecodeStatus Status)
    {
        switch (Status)
        {
        case ETSTikFinityJsonDecodeStatus::Decoded:
            return "Decoded";
        case ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent:
            return "IgnoredUnknownEvent";
        case ETSTikFinityJsonDecodeStatus::RejectedEmptyFrame:
            return "RejectedEmptyFrame";
        case ETSTikFinityJsonDecodeStatus::RejectedMalformedJson:
            return "RejectedMalformedJson";
        case ETSTikFinityJsonDecodeStatus::RejectedRootNotObject:
            return "RejectedRootNotObject";
        case ETSTikFinityJsonDecodeStatus::RejectedMissingEvent:
            return "RejectedMissingEvent";
        case ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName:
            return "RejectedInvalidEventName";
        case ETSTikFinityJsonDecodeStatus::RejectedMissingData:
            return "RejectedMissingData";
        case ETSTikFinityJsonDecodeStatus::RejectedDataNotObject:
            return "RejectedDataNotObject";
        case ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType:
            return "RejectedInvalidFieldType";
        case ETSTikFinityJsonDecodeStatus::RejectedNumericOutOfRange:
            return "RejectedNumericOutOfRange";
        case ETSTikFinityJsonDecodeStatus::RejectedInvalidArrayElement:
            return "RejectedInvalidArrayElement";
        }
        throw std::logic_error("Invalid TikFinity decode status");
    }

    [[nodiscard]]
    std::string DisplayName(ETSTikFinityMappedEventKind Kind)
    {
        switch (Kind)
        {
        case ETSTikFinityMappedEventKind::Chat:
            return "Chat";
        case ETSTikFinityMappedEventKind::Gift:
            return "Gift";
        case ETSTikFinityMappedEventKind::Like:
            return "Like";
        case ETSTikFinityMappedEventKind::Follow:
            return "Follow";
        case ETSTikFinityMappedEventKind::Share:
            return "Share";
        case ETSTikFinityMappedEventKind::RoomUser:
            return "RoomUser";
        case ETSTikFinityMappedEventKind::Member:
            return "Member";
        case ETSTikFinityMappedEventKind::Count:
            break;
        }
        throw std::logic_error("Invalid TikFinity event kind");
    }

    [[nodiscard]]
    std::string MakeCompactChecklist(
        const FTSTikFinitySevenEventChecklistSnapshot& Snapshot
    )
    {
        std::ostringstream Output;
        Output << "TikFinity event checklist\n\n";
        for (std::size_t Index = 0;
             Index < TSTikFinityMappedEventKindCount;
             ++Index)
        {
            const auto Kind = static_cast<ETSTikFinityMappedEventKind>(Index);
            Output << (Snapshot.Events[Index].bSeenValid ? "[X] " : "[ ] ")
                   << DisplayName(Kind) << '\n';
        }
        Output << "\nProgress: " << Snapshot.GetValidEventKindsSeen()
               << " / " << TSTikFinityMappedEventKindCount << '\n';
        return Output.str();
    }

    [[nodiscard]]
    std::string LimitRawFrame(std::string_view RawFrame)
    {
        if (RawFrame.size() <= MaximumRawFrameCharacters)
        {
            return std::string(RawFrame);
        }
        return std::string(RawFrame.substr(0, MaximumRawFrameCharacters)) +
            "...<truncated>";
    }

    void PrintBlock(FProbeState& State, const std::string& Text)
    {
        const std::lock_guard Lock(State.OutputMutex);
        std::cout << Text;
        if (Text.empty() || Text.back() != '\n')
        {
            std::cout << '\n';
        }
    }

    void PrintDecodedObservation(
        FProbeState& State,
        const FProbeOptions& Options,
        const FTSTikFinityJsonDecodeResult& Result,
        const FTSTikFinityChecklistObservation& Observation,
        const FTSTikFinitySevenEventChecklistSnapshot& Snapshot,
        std::string_view RawFrame
    )
    {
        std::ostringstream Output;
        switch (Observation.ObservationKind)
        {
        case ETSTikFinityChecklistObservationKind::ValidKnownEvent:
        {
            const std::size_t KindIndex =
                static_cast<std::size_t>(*Observation.EventKind);
            const std::uint64_t ValidCount =
                Snapshot.Events[KindIndex].ValidFrameCount;
            Output << "[OK] " << Result.EventName << " VALID";
            if (!Observation.bFirstValidObservation)
            {
                Output << " - repeat " << ValidCount;
            }
            Output << " - " << Observation.ValidEventKindsSeen << "/7\n";

            if (Options.bVerbose && Result.Event.has_value())
            {
                Output << FTSTikFinityMappedEventFormatter::Format(*Result.Event);
            }
            if (Observation.bFirstValidObservation || Options.bVerbose)
            {
                Output << '\n' << MakeCompactChecklist(Snapshot);
            }
            break;
        }
        case ETSTikFinityChecklistObservationKind::InvalidKnownEvent:
            Output << "[X] " << Result.EventName << " INVALID\n"
                   << "Status: " << DecodeStatusName(Result.Status) << '\n'
                   << "Path: " << Result.ErrorPath << '\n'
                   << "Raw: " << LimitRawFrame(RawFrame) << '\n'
                   << "Progress: " << Observation.ValidEventKindsSeen << " / 7\n\n"
                   << MakeCompactChecklist(Snapshot);
            break;
        case ETSTikFinityChecklistObservationKind::UnknownEvent:
            Output << "UNKNOWN EVENT: " << Result.EventName << '\n';
            break;
        case ETSTikFinityChecklistObservationKind::InvalidFrame:
            Output << "INVALID FRAME\n"
                   << "Status: " << DecodeStatusName(Result.Status) << '\n'
                   << "Path: " << Result.ErrorPath << '\n'
                   << "Raw: " << LimitRawFrame(RawFrame) << '\n';
            break;
        }
        PrintBlock(State, Output.str());
    }

    [[nodiscard]]
    std::string MakeFinalSummary(
        const FTSTikFinitySevenEventChecklistSnapshot& Snapshot
    )
    {
        std::ostringstream Output;
        Output << "\nTikFinity final summary\n\n";
        if (Snapshot.HasSeenAllSevenValid())
        {
            Output << "[OK] ALL SEVEN TIKFINITY EVENTS OBSERVED\n\n";
        }

        for (std::size_t Index = 0;
             Index < TSTikFinityMappedEventKindCount;
             ++Index)
        {
            const auto Kind = static_cast<ETSTikFinityMappedEventKind>(Index);
            const auto& Entry = Snapshot.Events[Index];
            Output << std::left << std::setw(10) << (DisplayName(Kind) + ":");
            if (Entry.bSeenValid)
            {
                Output << "VALID " << Entry.ValidFrameCount;
            }
            else
            {
                Output << "NOT SEEN";
            }
            Output << " | INVALID " << Entry.InvalidFrameCount << '\n';
        }

        Output << "\nProgress: " << Snapshot.GetValidEventKindsSeen() << " / 7\n"
               << "Known decoded events: " << Snapshot.KnownDecodedEvents << '\n'
               << "Unknown events: " << Snapshot.UnknownEvents << '\n'
               << "Invalid frames: " << Snapshot.InvalidFrames << '\n'
               << "Transport errors: " << Snapshot.TransportErrors << '\n'
               << "Binary frames: " << Snapshot.BinaryFrames << '\n';
        return Output.str();
    }
}

int main(int ArgumentCount, char* Arguments[])
{
    FProbeOptions Options;
    try
    {
        Options = ParseArguments(ArgumentCount, Arguments);
    }
    catch (const std::exception& Error)
    {
        std::cerr << "ARGUMENT ERROR: " << Error.what() << '\n';
        return 2;
    }

    try
    {
        FNetworkSystemGuard NetworkSystem;
        FProbeState State;
        ix::WebSocket WebSocket;
        WebSocket.setUrl(Options.Url);
        WebSocket.disableAutomaticReconnection();

        WebSocket.setOnMessageCallback(
            [&State, &Options](const ix::WebSocketMessagePtr& Message)
            {
                try
                {
                    if (Message->type == ix::WebSocketMessageType::Open)
                    {
                        FTSTikFinitySevenEventChecklistSnapshot Snapshot;
                        {
                            const std::lock_guard Lock(State.ChecklistMutex);
                            Snapshot = State.Checklist.GetSnapshot();
                        }
                        PrintBlock(
                            State,
                            "CONNECTED\n\n" + MakeCompactChecklist(Snapshot)
                        );
                    }
                    else if (Message->type == ix::WebSocketMessageType::Close)
                    {
                        PrintBlock(State, "DISCONNECTED\n");
                        State.bStopRequested.store(true);
                    }
                    else if (Message->type == ix::WebSocketMessageType::Error)
                    {
                        {
                            const std::lock_guard Lock(State.ChecklistMutex);
                            State.Checklist.RecordTransportError();
                        }
                        PrintBlock(
                            State,
                            "CONNECTION ERROR: " + Message->errorInfo.reason + "\n"
                        );
                        State.bStopRequested.store(true);
                    }
                    else if (Message->type == ix::WebSocketMessageType::Message)
                    {
                        if (Message->binary)
                        {
                            {
                                const std::lock_guard Lock(State.ChecklistMutex);
                                State.Checklist.RecordBinaryFrame();
                            }
                            PrintBlock(
                                State,
                                "BINARY FRAME: " +
                                    std::to_string(Message->str.size()) +
                                    " bytes\n"
                            );
                            return;
                        }

                        const FTSTikFinityJsonDecodeResult Result =
                            FTSTikFinityJsonEventDecoder::Decode(Message->str);
                        FTSTikFinityChecklistObservation Observation;
                        FTSTikFinitySevenEventChecklistSnapshot Snapshot;
                        {
                            const std::lock_guard Lock(State.ChecklistMutex);
                            Observation = State.Checklist.Observe(Result);
                            Snapshot = State.Checklist.GetSnapshot();
                        }

                        PrintDecodedObservation(
                            State,
                            Options,
                            Result,
                            Observation,
                            Snapshot,
                            Message->str
                        );
                        if (Options.bStopWhenAllSeen &&
                            Observation.bAllSevenSeenValid)
                        {
                            State.bStopRequested.store(true);
                        }
                    }
                }
                catch (const std::exception& Error)
                {
                    State.bInternalFailure.store(true);
                    State.bStopRequested.store(true);
                    PrintBlock(
                        State,
                        "INTERNAL ERROR: " + std::string(Error.what()) + "\n"
                    );
                }
                catch (...)
                {
                    State.bInternalFailure.store(true);
                    State.bStopRequested.store(true);
                    PrintBlock(State, "INTERNAL ERROR: unknown exception\n");
                }
            }
        );

        WebSocket.start();
        const auto Deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(Options.DurationSeconds);
        while (!State.bStopRequested.load() &&
               std::chrono::steady_clock::now() < Deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // stop() se ejecuta en el hilo principal, nunca dentro del callback.
        WebSocket.stop();

        FTSTikFinitySevenEventChecklistSnapshot FinalSnapshot;
        {
            const std::lock_guard Lock(State.ChecklistMutex);
            FinalSnapshot = State.Checklist.GetSnapshot();
        }
        PrintBlock(State, MakeFinalSummary(FinalSnapshot));

        if (State.bInternalFailure.load())
        {
            return 4;
        }
        if (Options.bRequireAllSeen && !FinalSnapshot.HasSeenAllSevenValid())
        {
            return 5;
        }
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "FATAL ERROR: " << Error.what() << '\n';
        return 3;
    }
    catch (...)
    {
        std::cerr << "FATAL ERROR: unknown exception\n";
        return 3;
    }
}
