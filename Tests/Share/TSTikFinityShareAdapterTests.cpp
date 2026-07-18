#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityShareConverter.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedShareMessage MakeMinimalValidShareMessage()
    {
        FTSTikFinityDecodedShareMessage Message;
        Message.EventName = "share";
        Message.Data.emplace();
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "share-user-42";
        return Message;
    }

    void RequireShareConversionStatus(
        const FTSTikFinityShareConversionResult& Result,
        ETSTikFinityShareConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityShareConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void RequireShareUser(
        const FTSUserSnapshot& User,
        const std::string& UniqueId,
        const std::string& Context
    )
    {
        Require(User.UniqueId == UniqueId, Context + ": UniqueId");
        Require(User.Nickname == "Share User", Context + ": Nickname");
        Require(
            User.ProfilePictureUrl == "https://example.test/share.png",
            Context + ": ProfilePictureUrl"
        );
        Require(User.FollowRole == 3, Context + ": FollowRole");
        Require(User.bIsModerator, Context + ": bIsModerator");
        Require(!User.bIsSubscriber, Context + ": bIsSubscriber");
        Require(User.bIsNewGifter, Context + ": bIsNewGifter");
        Require(User.TopGifterRank == 7, Context + ": TopGifterRank");
        Require(User.GifterLevel == 11, Context + ": GifterLevel");
        Require(
            User.TeamMemberLevel == 13,
            Context + ": TeamMemberLevel"
        );
    }

    void TestCompleteShareConversion()
    {
        FTSTikFinityDecodedShareMessage Message =
            MakeMinimalValidShareMessage();
        FTSTikFinityDecodedUser& User = *Message.Data->User;
        User.UniqueId = "complete-share-user";
        User.Nickname = "Share User";
        User.ProfilePictureUrl = "https://example.test/share.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;

        const FTSTikFinityShareConversionResult Result =
            FTSTikFinityShareConverter::Convert(Message);
        RequireShareConversionStatus(
            Result,
            ETSTikFinityShareConversionStatus::Converted,
            "Complete Share conversion"
        );
        RequireShareUser(
            Result.Input->User,
            "complete-share-user",
            "Complete Share"
        );
    }

    void TestShareOptionalFieldDefaults()
    {
        const FTSTikFinityShareConversionResult Result =
            FTSTikFinityShareConverter::Convert(
                MakeMinimalValidShareMessage()
            );
        RequireShareConversionStatus(
            Result,
            ETSTikFinityShareConversionStatus::Converted,
            "Share optional defaults"
        );

        const FTSUserSnapshot& User = Result.Input->User;
        Require(User.UniqueId == "share-user-42", "Share default UniqueId");
        Require(User.Nickname.empty(), "Share default Nickname");
        Require(
            User.ProfilePictureUrl.empty(),
            "Share default ProfilePictureUrl"
        );
        Require(User.FollowRole == 0, "Share default FollowRole");
        Require(!User.bIsModerator, "Share default bIsModerator");
        Require(!User.bIsSubscriber, "Share default bIsSubscriber");
        Require(!User.bIsNewGifter, "Share default bIsNewGifter");
        Require(User.TopGifterRank == 0, "Share default TopGifterRank");
        Require(User.GifterLevel == 0, "Share default GifterLevel");
        Require(
            User.TeamMemberLevel == 0,
            "Share default TeamMemberLevel"
        );
    }

    void TestNonShareEventIsIgnored()
    {
        FTSTikFinityDecodedShareMessage Message;
        Message.EventName = "follow";

        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(Message),
            ETSTikFinityShareConversionStatus::IgnoredNonShareEvent,
            "Non-Share event"
        );
    }

    void TestShareInvalidEnvelopeAndMissingDataOrUser()
    {
        FTSTikFinityDecodedShareMessage EmptyEnvelope;
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(EmptyEnvelope),
            ETSTikFinityShareConversionStatus::RejectedInvalidEnvelope,
            "Empty Share envelope"
        );

        FTSTikFinityDecodedShareMessage MissingData;
        MissingData.EventName = "share";
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(MissingData),
            ETSTikFinityShareConversionStatus::RejectedMissingData,
            "Missing Share data"
        );

        FTSTikFinityDecodedShareMessage MissingUser;
        MissingUser.EventName = "share";
        MissingUser.Data.emplace();
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(MissingUser),
            ETSTikFinityShareConversionStatus::RejectedMissingUser,
            "Missing Share user"
        );
    }

    void TestShareMissingOrEmptyIdentity()
    {
        FTSTikFinityDecodedShareMessage MissingIdentity =
            MakeMinimalValidShareMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(MissingIdentity),
            ETSTikFinityShareConversionStatus::RejectedMissingUserIdentity,
            "Missing Share identity"
        );

        FTSTikFinityDecodedShareMessage EmptyIdentity =
            MakeMinimalValidShareMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(EmptyIdentity),
            ETSTikFinityShareConversionStatus::RejectedMissingUserIdentity,
            "Empty Share identity"
        );
    }

    void TestShareNumericRepresentationBoundaries()
    {
        FTSTikFinityDecodedShareMessage Zero =
            MakeMinimalValidShareMessage();
        Zero.Data->User->FollowRole = 0;
        Zero.Data->User->TopGifterRank = 0;
        Zero.Data->User->GifterLevel = 0;
        Zero.Data->User->TeamMemberLevel = 0;
        const FTSTikFinityShareConversionResult ZeroResult =
            FTSTikFinityShareConverter::Convert(Zero);
        RequireShareConversionStatus(
            ZeroResult,
            ETSTikFinityShareConversionStatus::Converted,
            "Share numeric zero"
        );
        Require(
            ZeroResult.Input->User.FollowRole == 0 &&
                ZeroResult.Input->User.TopGifterRank == 0 &&
                ZeroResult.Input->User.GifterLevel == 0 &&
                ZeroResult.Input->User.TeamMemberLevel == 0,
            "Share numeric zero values"
        );

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedShareMessage Maximum =
            MakeMinimalValidShareMessage();
        Maximum.Data->User->FollowRole = Int32Max;
        Maximum.Data->User->TopGifterRank = Int32Max;
        Maximum.Data->User->GifterLevel = Int32Max;
        Maximum.Data->User->TeamMemberLevel = Int32Max;
        const FTSTikFinityShareConversionResult MaximumResult =
            FTSTikFinityShareConverter::Convert(Maximum);
        RequireShareConversionStatus(
            MaximumResult,
            ETSTikFinityShareConversionStatus::Converted,
            "Share numeric INT32_MAX"
        );
        Require(
            MaximumResult.Input->User.FollowRole ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TopGifterRank ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.GifterLevel ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TeamMemberLevel ==
                    std::numeric_limits<std::int32_t>::max(),
            "Share numeric INT32_MAX values"
        );

        FTSTikFinityDecodedShareMessage Negative =
            MakeMinimalValidShareMessage();
        Negative.Data->User->TopGifterRank = -1;
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(Negative),
            ETSTikFinityShareConversionStatus::RejectedInvalidNumericField,
            "Negative Share numeric"
        );

        FTSTikFinityDecodedShareMessage AboveMaximum =
            MakeMinimalValidShareMessage();
        AboveMaximum.Data->User->GifterLevel = Int32Max + 1;
        RequireShareConversionStatus(
            FTSTikFinityShareConverter::Convert(AboveMaximum),
            ETSTikFinityShareConversionStatus::RejectedInvalidNumericField,
            "Above-max Share numeric"
        );
    }

    void TestJsonDecodeToShareInput()
    {
        constexpr const char* Json = R"json(
{
  "event": "share",
  "data": {
    "uniqueId": "json-share-user",
    "nickname": "Share User",
    "profilePictureUrl": "https://example.test/share.png",
    "followRole": 3,
    "isModerator": true,
    "isSubscriber": false,
    "isNewGifter": true,
    "topGifterRank": 7,
    "gifterLevel": 11,
    "teamMemberLevel": 13
  }
}
)json";

        const FTSTikFinityJsonDecodeResult Decoded =
            FTSTikFinityJsonEventDecoder::Decode(Json);
        Require(
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded &&
                Decoded.Event.has_value(),
            "Share JSON must decode"
        );

        const FTSTikFinityDecodedShareMessage* ShareMessage =
            std::get_if<FTSTikFinityDecodedShareMessage>(&*Decoded.Event);
        Require(ShareMessage != nullptr, "Decoded variant must contain Share");

        const FTSTikFinityShareConversionResult Converted =
            FTSTikFinityShareConverter::Convert(*ShareMessage);
        RequireShareConversionStatus(
            Converted,
            ETSTikFinityShareConversionStatus::Converted,
            "Decoded Share conversion"
        );
        RequireShareUser(
            Converted.Input->User,
            "json-share-user",
            "Decoded Share input"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityShareAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Complete Share conversion", &TestCompleteShareConversion});
        Tests.push_back({"Share optional field defaults", &TestShareOptionalFieldDefaults});
        Tests.push_back({"Non-Share event is ignored", &TestNonShareEventIsIgnored});
        Tests.push_back({"Invalid envelope and missing data or user", &TestShareInvalidEnvelopeAndMissingDataOrUser});
        Tests.push_back({"Missing or empty Share identity", &TestShareMissingOrEmptyIdentity});
        Tests.push_back({"Share numeric representation boundaries", &TestShareNumericRepresentationBoundaries});
        Tests.push_back({"JSON Share decode and converter integration", &TestJsonDecodeToShareInput});
    }
}
