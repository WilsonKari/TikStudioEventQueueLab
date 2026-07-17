#include "TikFinity/TSTikFinityFollowConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedFollowMessage MakeMinimalValidFollowMessage()
    {
        FTSTikFinityDecodedFollowMessage Message;
        Message.EventName = "follow";
        Message.Data.emplace();
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "follow-user-42";
        return Message;
    }

    void RequireFollowConversionStatus(
        const FTSTikFinityFollowConversionResult& Result,
        ETSTikFinityFollowConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityFollowConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void RequireFollowUser(
        const FTSUserSnapshot& User,
        const std::string& UniqueId,
        const std::string& Context
    )
    {
        Require(User.UniqueId == UniqueId, Context + ": UniqueId");
        Require(User.Nickname == "Follow User", Context + ": Nickname");
        Require(
            User.ProfilePictureUrl == "https://example.test/follow.png",
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

    void TestCompleteFollowConversion()
    {
        FTSTikFinityDecodedFollowMessage Message =
            MakeMinimalValidFollowMessage();
        FTSTikFinityDecodedUser& User = *Message.Data->User;
        User.UniqueId = "complete-follow-user";
        User.Nickname = "Follow User";
        User.ProfilePictureUrl = "https://example.test/follow.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;

        const FTSTikFinityFollowConversionResult Result =
            FTSTikFinityFollowConverter::Convert(Message);
        RequireFollowConversionStatus(
            Result,
            ETSTikFinityFollowConversionStatus::Converted,
            "Complete Follow conversion"
        );
        RequireFollowUser(
            Result.Input->User,
            "complete-follow-user",
            "Complete Follow"
        );
    }

    void TestFollowOptionalFieldDefaults()
    {
        const FTSTikFinityFollowConversionResult Result =
            FTSTikFinityFollowConverter::Convert(
                MakeMinimalValidFollowMessage()
            );
        RequireFollowConversionStatus(
            Result,
            ETSTikFinityFollowConversionStatus::Converted,
            "Follow optional defaults"
        );

        const FTSUserSnapshot& User = Result.Input->User;
        Require(User.UniqueId == "follow-user-42", "Follow default UniqueId");
        Require(User.Nickname.empty(), "Follow default Nickname");
        Require(
            User.ProfilePictureUrl.empty(),
            "Follow default ProfilePictureUrl"
        );
        Require(User.FollowRole == 0, "Follow default FollowRole");
        Require(!User.bIsModerator, "Follow default bIsModerator");
        Require(!User.bIsSubscriber, "Follow default bIsSubscriber");
        Require(!User.bIsNewGifter, "Follow default bIsNewGifter");
        Require(User.TopGifterRank == 0, "Follow default TopGifterRank");
        Require(User.GifterLevel == 0, "Follow default GifterLevel");
        Require(
            User.TeamMemberLevel == 0,
            "Follow default TeamMemberLevel"
        );
    }

    void TestNonFollowEventIsIgnoredWithoutFollowInspection()
    {
        FTSTikFinityDecodedFollowMessage Message;
        Message.EventName = "share";

        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(Message),
            ETSTikFinityFollowConversionStatus::IgnoredNonFollowEvent,
            "Non-Follow event"
        );
    }

    void TestFollowInvalidEnvelopeAndMissingDataOrUser()
    {
        FTSTikFinityDecodedFollowMessage EmptyEnvelope;
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(EmptyEnvelope),
            ETSTikFinityFollowConversionStatus::RejectedInvalidEnvelope,
            "Empty Follow envelope"
        );

        FTSTikFinityDecodedFollowMessage MissingData;
        MissingData.EventName = "follow";
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(MissingData),
            ETSTikFinityFollowConversionStatus::RejectedMissingData,
            "Missing Follow data"
        );

        FTSTikFinityDecodedFollowMessage MissingUser;
        MissingUser.EventName = "follow";
        MissingUser.Data.emplace();
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(MissingUser),
            ETSTikFinityFollowConversionStatus::RejectedMissingUser,
            "Missing Follow user"
        );
    }

    void TestFollowMissingOrEmptyUserIdentity()
    {
        FTSTikFinityDecodedFollowMessage MissingIdentity =
            MakeMinimalValidFollowMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(MissingIdentity),
            ETSTikFinityFollowConversionStatus::RejectedMissingUserIdentity,
            "Missing Follow identity"
        );

        FTSTikFinityDecodedFollowMessage EmptyIdentity =
            MakeMinimalValidFollowMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(EmptyIdentity),
            ETSTikFinityFollowConversionStatus::RejectedMissingUserIdentity,
            "Empty Follow identity"
        );
    }

    void TestFollowNumericRepresentationBoundaries()
    {
        FTSTikFinityDecodedFollowMessage Zero =
            MakeMinimalValidFollowMessage();
        Zero.Data->User->FollowRole = 0;
        Zero.Data->User->TopGifterRank = 0;
        Zero.Data->User->GifterLevel = 0;
        Zero.Data->User->TeamMemberLevel = 0;
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(Zero),
            ETSTikFinityFollowConversionStatus::Converted,
            "Follow numeric zero"
        );

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedFollowMessage Maximum =
            MakeMinimalValidFollowMessage();
        Maximum.Data->User->FollowRole = Int32Max;
        Maximum.Data->User->TopGifterRank = Int32Max;
        Maximum.Data->User->GifterLevel = Int32Max;
        Maximum.Data->User->TeamMemberLevel = Int32Max;
        const FTSTikFinityFollowConversionResult MaximumResult =
            FTSTikFinityFollowConverter::Convert(Maximum);
        RequireFollowConversionStatus(
            MaximumResult,
            ETSTikFinityFollowConversionStatus::Converted,
            "Follow numeric INT32_MAX"
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
            "Follow numeric INT32_MAX values"
        );

        FTSTikFinityDecodedFollowMessage Negative =
            MakeMinimalValidFollowMessage();
        Negative.Data->User->TopGifterRank = -1;
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(Negative),
            ETSTikFinityFollowConversionStatus::RejectedInvalidNumericField,
            "Negative Follow numeric"
        );

        FTSTikFinityDecodedFollowMessage AboveMaximum =
            MakeMinimalValidFollowMessage();
        AboveMaximum.Data->User->GifterLevel = Int32Max + 1;
        RequireFollowConversionStatus(
            FTSTikFinityFollowConverter::Convert(AboveMaximum),
            ETSTikFinityFollowConversionStatus::RejectedInvalidNumericField,
            "Above-max Follow numeric"
        );
    }

    void TestJsonDecodeToFollowInput()
    {
        constexpr const char* Json = R"json(
{
  "event": "follow",
  "data": {
    "uniqueId": "json-follow-user",
    "nickname": "Follow User",
    "profilePictureUrl": "https://example.test/follow.png",
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
            "Follow JSON must decode"
        );

        const FTSTikFinityDecodedFollowMessage* FollowMessage =
            std::get_if<FTSTikFinityDecodedFollowMessage>(&*Decoded.Event);
        Require(FollowMessage != nullptr, "Decoded variant must contain Follow");

        const FTSTikFinityFollowConversionResult Converted =
            FTSTikFinityFollowConverter::Convert(*FollowMessage);
        RequireFollowConversionStatus(
            Converted,
            ETSTikFinityFollowConversionStatus::Converted,
            "Decoded Follow conversion"
        );
        RequireFollowUser(
            Converted.Input->User,
            "json-follow-user",
            "Decoded Follow input"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityFollowAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Complete Follow conversion", &TestCompleteFollowConversion});
        Tests.push_back({"Follow optional field defaults", &TestFollowOptionalFieldDefaults});
        Tests.push_back({"Non-Follow event is ignored", &TestNonFollowEventIsIgnoredWithoutFollowInspection});
        Tests.push_back({"Invalid envelope and missing data or user", &TestFollowInvalidEnvelopeAndMissingDataOrUser});
        Tests.push_back({"Missing or empty Follow identity", &TestFollowMissingOrEmptyUserIdentity});
        Tests.push_back({"Follow numeric representation boundaries", &TestFollowNumericRepresentationBoundaries});
        Tests.push_back({"JSON Follow decode and converter integration", &TestJsonDecodeToFollowInput});
    }
}
