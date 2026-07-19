#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityRoomUserConverter.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedRoomUserMessage MakeMinimalValidRoomUserMessage()
    {
        FTSTikFinityDecodedRoomUserMessage Message;
        Message.EventName = "roomUser";
        Message.Data.emplace();
        Message.Data->ViewerCount = 0;
        return Message;
    }

    [[nodiscard]]
    FTSTikFinityDecodedTopViewer MakeTopViewer(
        std::int64_t CoinCount,
        std::string UniqueId
    )
    {
        FTSTikFinityDecodedTopViewer Viewer;
        Viewer.CoinCount = CoinCount;
        Viewer.User.emplace();
        Viewer.User->UniqueId = std::move(UniqueId);
        return Viewer;
    }

    [[nodiscard]]
    FTSTikFinityDecodedRoomUserMessage MakeCompleteRoomUserMessage()
    {
        FTSTikFinityDecodedRoomUserMessage Message =
            MakeMinimalValidRoomUserMessage();
        Message.Data->ViewerCount = 100;
        Message.Data->TopGifterRank = 7;

        FTSTikFinityDecodedTopViewer First = MakeTopViewer(11, "viewer-a");
        First.User->Nickname = "Viewer A";
        First.User->ProfilePictureUrl = "https://example.test/a.png";
        First.User->bIsModerator = true;
        First.User->bIsSubscriber = false;
        First.User->GifterLevel = 1;
        First.User->TeamMemberLevel = 2;

        FTSTikFinityDecodedTopViewer Second = MakeTopViewer(22, "viewer-b");
        Second.User->Nickname = "Viewer B";
        Second.User->ProfilePictureUrl = "https://example.test/b.png";
        Second.User->bIsModerator = false;
        Second.User->bIsSubscriber = true;
        Second.User->GifterLevel = 3;
        Second.User->TeamMemberLevel = 4;

        Message.Data->TopViewers.push_back(std::move(First));
        Message.Data->TopViewers.push_back(std::move(Second));
        return Message;
    }

    void RequireRoomUserConversionStatus(
        const FTSTikFinityRoomUserConversionResult& Result,
        ETSTikFinityRoomUserConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityRoomUserConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void RequireTopViewer(
        const FTSRoomUserTopViewer& Viewer,
        const std::string& UniqueId,
        const std::string& Nickname,
        const std::string& ProfilePictureUrl,
        std::int32_t CoinCount,
        bool bIsModerator,
        bool bIsSubscriber,
        std::int32_t GifterLevel,
        std::int32_t TeamMemberLevel,
        const std::string& Context
    )
    {
        Require(Viewer.UniqueId == UniqueId, Context + ": UniqueId");
        Require(Viewer.Nickname == Nickname, Context + ": Nickname");
        Require(
            Viewer.ProfilePictureUrl == ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(Viewer.CoinCount == CoinCount, Context + ": CoinCount");
        Require(
            Viewer.bIsModerator == bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            Viewer.bIsSubscriber == bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(Viewer.GifterLevel == GifterLevel, Context + ": GifterLevel");
        Require(
            Viewer.TeamMemberLevel == TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    void TestCompleteRoomUserConversion()
    {
        const FTSTikFinityRoomUserConversionResult Result =
            FTSTikFinityRoomUserConverter::Convert(
                MakeCompleteRoomUserMessage()
            );
        RequireRoomUserConversionStatus(
            Result,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "Complete RoomUser conversion"
        );

        const FTSRoomUserInput& Input = *Result.Input;
        Require(Input.ViewerCount == 100, "Complete RoomUser ViewerCount");
        Require(Input.TopGifterRank == 7, "Complete RoomUser TopGifterRank");
        Require(Input.TopViewers.size() == 2, "Complete RoomUser viewer count");
        RequireTopViewer(
            Input.TopViewers[0],
            "viewer-a",
            "Viewer A",
            "https://example.test/a.png",
            11,
            true,
            false,
            1,
            2,
            "Complete RoomUser viewer A"
        );
        RequireTopViewer(
            Input.TopViewers[1],
            "viewer-b",
            "Viewer B",
            "https://example.test/b.png",
            22,
            false,
            true,
            3,
            4,
            "Complete RoomUser viewer B"
        );
    }

    void TestRoomUserMinimalDefaultsAndEmptyTopViewers()
    {
        const FTSTikFinityRoomUserConversionResult Result =
            FTSTikFinityRoomUserConverter::Convert(
                MakeMinimalValidRoomUserMessage()
            );
        RequireRoomUserConversionStatus(
            Result,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "Minimal RoomUser"
        );
        Require(
            Result.Input->ViewerCount == 0 &&
                Result.Input->TopGifterRank == 0 &&
                Result.Input->TopViewers.empty(),
            "Minimal RoomUser defaults"
        );
    }

    void TestRoomUserTopViewerOptionalFieldDefaults()
    {
        FTSTikFinityDecodedRoomUserMessage Message =
            MakeMinimalValidRoomUserMessage();
        Message.Data->TopViewers.push_back(
            MakeTopViewer(9, "minimal-viewer")
        );

        const FTSTikFinityRoomUserConversionResult Result =
            FTSTikFinityRoomUserConverter::Convert(Message);
        RequireRoomUserConversionStatus(
            Result,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "RoomUser TopViewer defaults"
        );
        Require(Result.Input->TopViewers.size() == 1, "Default viewer count");
        RequireTopViewer(
            Result.Input->TopViewers[0],
            "minimal-viewer",
            "",
            "",
            9,
            false,
            false,
            0,
            0,
            "Default RoomUser viewer"
        );
    }

    void TestNonRoomUserEventIsIgnored()
    {
        FTSTikFinityDecodedRoomUserMessage Message;
        Message.EventName = "like";
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(Message),
            ETSTikFinityRoomUserConversionStatus::IgnoredNonRoomUserEvent,
            "Non-RoomUser event"
        );
    }

    void TestInvalidEnvelopeAndMissingRoomUserTopLevelData()
    {
        FTSTikFinityDecodedRoomUserMessage EmptyEnvelope;
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(EmptyEnvelope),
            ETSTikFinityRoomUserConversionStatus::RejectedInvalidEnvelope,
            "Empty RoomUser envelope"
        );

        FTSTikFinityDecodedRoomUserMessage MissingData;
        MissingData.EventName = "roomUser";
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(MissingData),
            ETSTikFinityRoomUserConversionStatus::RejectedMissingData,
            "Missing RoomUser data"
        );

        FTSTikFinityDecodedRoomUserMessage MissingViewerCount;
        MissingViewerCount.EventName = "roomUser";
        MissingViewerCount.Data.emplace();
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(MissingViewerCount),
            ETSTikFinityRoomUserConversionStatus::RejectedMissingViewerCount,
            "Missing RoomUser ViewerCount"
        );
    }

    void TestRoomUserTopViewerStructuralRequirements()
    {
        FTSTikFinityDecodedRoomUserMessage MissingCoinCount =
            MakeMinimalValidRoomUserMessage();
        MissingCoinCount.Data->TopViewers.emplace_back();
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(MissingCoinCount),
            ETSTikFinityRoomUserConversionStatus::
                RejectedMissingTopViewerCoinCount,
            "Missing TopViewer CoinCount"
        );

        FTSTikFinityDecodedRoomUserMessage MissingUser =
            MakeMinimalValidRoomUserMessage();
        FTSTikFinityDecodedTopViewer ViewerWithoutUser;
        ViewerWithoutUser.CoinCount = 1;
        MissingUser.Data->TopViewers.push_back(ViewerWithoutUser);
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(MissingUser),
            ETSTikFinityRoomUserConversionStatus::RejectedMissingTopViewerUser,
            "Missing TopViewer user"
        );

        FTSTikFinityDecodedRoomUserMessage MissingIdentity =
            MakeMinimalValidRoomUserMessage();
        FTSTikFinityDecodedTopViewer ViewerWithoutIdentity;
        ViewerWithoutIdentity.CoinCount = 1;
        ViewerWithoutIdentity.User.emplace();
        MissingIdentity.Data->TopViewers.push_back(ViewerWithoutIdentity);
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(MissingIdentity),
            ETSTikFinityRoomUserConversionStatus::
                RejectedMissingTopViewerIdentity,
            "Missing TopViewer identity"
        );

        FTSTikFinityDecodedRoomUserMessage EmptyIdentity =
            MakeMinimalValidRoomUserMessage();
        EmptyIdentity.Data->TopViewers.push_back(MakeTopViewer(1, ""));
        RequireRoomUserConversionStatus(
            FTSTikFinityRoomUserConverter::Convert(EmptyIdentity),
            ETSTikFinityRoomUserConversionStatus::
                RejectedMissingTopViewerIdentity,
            "Empty TopViewer identity"
        );
    }

    void TestRoomUserNumericRepresentationBoundaries()
    {
        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();

        FTSTikFinityDecodedRoomUserMessage Zero =
            MakeMinimalValidRoomUserMessage();
        Zero.Data->TopGifterRank = 0;
        Zero.Data->TopViewers.push_back(MakeTopViewer(0, "zero-viewer"));
        Zero.Data->TopViewers[0].User->GifterLevel = 0;
        Zero.Data->TopViewers[0].User->TeamMemberLevel = 0;
        const FTSTikFinityRoomUserConversionResult ZeroResult =
            FTSTikFinityRoomUserConverter::Convert(Zero);
        RequireRoomUserConversionStatus(
            ZeroResult,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "RoomUser numeric zero"
        );
        Require(
            ZeroResult.Input->ViewerCount == 0 &&
                ZeroResult.Input->TopGifterRank == 0 &&
                ZeroResult.Input->TopViewers[0].CoinCount == 0 &&
                ZeroResult.Input->TopViewers[0].GifterLevel == 0 &&
                ZeroResult.Input->TopViewers[0].TeamMemberLevel == 0,
            "RoomUser zero numeric values"
        );

        FTSTikFinityDecodedRoomUserMessage Maximum =
            MakeMinimalValidRoomUserMessage();
        Maximum.Data->ViewerCount = Int32Max;
        Maximum.Data->TopGifterRank = Int32Max;
        Maximum.Data->TopViewers.push_back(
            MakeTopViewer(Int32Max, "maximum-viewer")
        );
        Maximum.Data->TopViewers[0].User->GifterLevel = Int32Max;
        Maximum.Data->TopViewers[0].User->TeamMemberLevel = Int32Max;
        const FTSTikFinityRoomUserConversionResult MaximumResult =
            FTSTikFinityRoomUserConverter::Convert(Maximum);
        RequireRoomUserConversionStatus(
            MaximumResult,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "RoomUser numeric INT32_MAX"
        );
        Require(
            MaximumResult.Input->ViewerCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->TopGifterRank ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->TopViewers[0].CoinCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->TopViewers[0].GifterLevel ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->TopViewers[0].TeamMemberLevel ==
                    std::numeric_limits<std::int32_t>::max(),
            "RoomUser INT32_MAX numeric values"
        );

        const auto RequireInvalid = [](
            FTSTikFinityDecodedRoomUserMessage Message,
            const std::string& Context
        )
        {
            RequireRoomUserConversionStatus(
                FTSTikFinityRoomUserConverter::Convert(Message),
                ETSTikFinityRoomUserConversionStatus::
                    RejectedInvalidNumericField,
                Context
            );
        };

        const std::int64_t InvalidValues[]{-1, Int32Max + 1};
        for (const std::int64_t Invalid : InvalidValues)
        {
            FTSTikFinityDecodedRoomUserMessage ViewerCount =
                MakeMinimalValidRoomUserMessage();
            ViewerCount.Data->ViewerCount = Invalid;
            RequireInvalid(ViewerCount, "Invalid ViewerCount");

            FTSTikFinityDecodedRoomUserMessage TopGifterRank =
                MakeMinimalValidRoomUserMessage();
            TopGifterRank.Data->TopGifterRank = Invalid;
            RequireInvalid(TopGifterRank, "Invalid TopGifterRank");

            FTSTikFinityDecodedRoomUserMessage CoinCount =
                MakeMinimalValidRoomUserMessage();
            CoinCount.Data->TopViewers.push_back(
                MakeTopViewer(Invalid, "invalid-coin-viewer")
            );
            RequireInvalid(CoinCount, "Invalid CoinCount");

            FTSTikFinityDecodedRoomUserMessage GifterLevel =
                MakeMinimalValidRoomUserMessage();
            GifterLevel.Data->TopViewers.push_back(
                MakeTopViewer(1, "invalid-gifter-viewer")
            );
            GifterLevel.Data->TopViewers[0].User->GifterLevel = Invalid;
            RequireInvalid(GifterLevel, "Invalid GifterLevel");

            FTSTikFinityDecodedRoomUserMessage TeamMemberLevel =
                MakeMinimalValidRoomUserMessage();
            TeamMemberLevel.Data->TopViewers.push_back(
                MakeTopViewer(1, "invalid-team-viewer")
            );
            TeamMemberLevel.Data->TopViewers[0].User->TeamMemberLevel = Invalid;
            RequireInvalid(TeamMemberLevel, "Invalid TeamMemberLevel");
        }
    }

    void TestRoomUserTopViewerOrderAndMultiplicityPreservation()
    {
        FTSTikFinityDecodedRoomUserMessage Message =
            MakeMinimalValidRoomUserMessage();
        Message.Data->TopViewers.push_back(MakeTopViewer(5, "duplicate"));
        Message.Data->TopViewers.push_back(MakeTopViewer(5, "duplicate"));
        Message.Data->TopViewers.push_back(MakeTopViewer(2, "last"));

        const FTSTikFinityRoomUserConversionResult Result =
            FTSTikFinityRoomUserConverter::Convert(Message);
        RequireRoomUserConversionStatus(
            Result,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "RoomUser order and multiplicity"
        );
        Require(
            Result.Input->TopViewers.size() == 3 &&
                Result.Input->TopViewers[0].UniqueId == "duplicate" &&
                Result.Input->TopViewers[0].CoinCount == 5 &&
                Result.Input->TopViewers[1].UniqueId == "duplicate" &&
                Result.Input->TopViewers[1].CoinCount == 5 &&
                Result.Input->TopViewers[2].UniqueId == "last" &&
                Result.Input->TopViewers[2].CoinCount == 2,
            "RoomUser must preserve TopViewer order and duplicates"
        );
    }

    void TestRoomUserValuesHaveNoInferredRelations()
    {
        FTSTikFinityDecodedRoomUserMessage Message =
            MakeMinimalValidRoomUserMessage();
        Message.Data->ViewerCount = 1;
        Message.Data->TopGifterRank = 99;
        Message.Data->TopViewers.push_back(MakeTopViewer(5, "relation-a"));
        Message.Data->TopViewers.push_back(MakeTopViewer(20, "relation-b"));
        Message.Data->TopViewers.push_back(MakeTopViewer(3, "relation-c"));

        const FTSTikFinityRoomUserConversionResult Result =
            FTSTikFinityRoomUserConverter::Convert(Message);
        RequireRoomUserConversionStatus(
            Result,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "RoomUser unrelated values"
        );
        Require(
            Result.Input->ViewerCount == 1 &&
                Result.Input->TopGifterRank == 99 &&
                Result.Input->TopViewers.size() == 3 &&
                Result.Input->TopViewers[0].CoinCount == 5 &&
                Result.Input->TopViewers[1].CoinCount == 20 &&
                Result.Input->TopViewers[2].CoinCount == 3,
            "RoomUser values must not be corrected or inferred"
        );
    }

    void TestJsonRoomUserDecodeAndConverterIntegration()
    {
        constexpr const char* Json = R"json(
{
  "event": "roomUser",
  "data": {
    "viewerCount": 100,
    "topGifterRank": 7,
    "topViewers": [
      {
        "coinCount": 11,
        "user": {
          "uniqueId": "json-viewer-a",
          "nickname": "JSON Viewer A",
          "profilePictureUrl": "https://example.test/json-a.png",
          "isModerator": true,
          "isSubscriber": false,
          "gifterLevel": 1,
          "teamMemberLevel": 2
        }
      },
      {
        "coinCount": 22,
        "user": {
          "uniqueId": "json-viewer-b",
          "nickname": "JSON Viewer B",
          "profilePictureUrl": "https://example.test/json-b.png",
          "isModerator": false,
          "isSubscriber": true,
          "gifterLevel": 3,
          "teamMemberLevel": 4
        }
      }
    ]
  }
}
)json";

        const FTSTikFinityJsonDecodeResult Decoded =
            FTSTikFinityJsonEventDecoder::Decode(Json);
        Require(
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded &&
                Decoded.Event.has_value(),
            "RoomUser JSON must decode"
        );

        const FTSTikFinityDecodedRoomUserMessage* RoomUserMessage =
            std::get_if<FTSTikFinityDecodedRoomUserMessage>(&*Decoded.Event);
        Require(
            RoomUserMessage != nullptr,
            "Decoded variant must contain RoomUser"
        );

        const FTSTikFinityRoomUserConversionResult Converted =
            FTSTikFinityRoomUserConverter::Convert(*RoomUserMessage);
        RequireRoomUserConversionStatus(
            Converted,
            ETSTikFinityRoomUserConversionStatus::Converted,
            "Decoded RoomUser conversion"
        );
        Require(
            Converted.Input->ViewerCount == 100 &&
                Converted.Input->TopGifterRank == 7 &&
                Converted.Input->TopViewers.size() == 2,
            "Decoded RoomUser top-level values"
        );
        RequireTopViewer(
            Converted.Input->TopViewers[0],
            "json-viewer-a",
            "JSON Viewer A",
            "https://example.test/json-a.png",
            11,
            true,
            false,
            1,
            2,
            "Decoded RoomUser viewer A"
        );
        RequireTopViewer(
            Converted.Input->TopViewers[1],
            "json-viewer-b",
            "JSON Viewer B",
            "https://example.test/json-b.png",
            22,
            false,
            true,
            3,
            4,
            "Decoded RoomUser viewer B"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityRoomUserAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Complete RoomUser conversion",
            &TestCompleteRoomUserConversion
        });
        Tests.push_back({
            "RoomUser minimal defaults and empty TopViewers",
            &TestRoomUserMinimalDefaultsAndEmptyTopViewers
        });
        Tests.push_back({
            "RoomUser TopViewer optional field defaults",
            &TestRoomUserTopViewerOptionalFieldDefaults
        });
        Tests.push_back({
            "Non-RoomUser event is ignored",
            &TestNonRoomUserEventIsIgnored
        });
        Tests.push_back({
            "Invalid envelope and missing RoomUser top-level data",
            &TestInvalidEnvelopeAndMissingRoomUserTopLevelData
        });
        Tests.push_back({
            "RoomUser TopViewer structural requirements",
            &TestRoomUserTopViewerStructuralRequirements
        });
        Tests.push_back({
            "RoomUser numeric representation boundaries",
            &TestRoomUserNumericRepresentationBoundaries
        });
        Tests.push_back({
            "RoomUser TopViewer order and multiplicity preservation",
            &TestRoomUserTopViewerOrderAndMultiplicityPreservation
        });
        Tests.push_back({
            "RoomUser values have no inferred relations",
            &TestRoomUserValuesHaveNoInferredRelations
        });
        Tests.push_back({
            "JSON RoomUser decode and converter integration",
            &TestJsonRoomUserDecodeAndConverterIntegration
        });
    }
}
