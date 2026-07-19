#include "TikFinity/TSTikFinityMemberConverter.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedMemberMessage MakeMinimalValidMemberMessage()
    {
        FTSTikFinityDecodedMemberMessage Message;
        Message.EventName = "member";
        Message.Data.emplace();
        Message.Data->ActionId = 1;
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "member-user-42";
        return Message;
    }

    void RequireMemberConversionStatus(
        const FTSTikFinityMemberConversionResult& Result,
        ETSTikFinityMemberConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityMemberConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void TestCompleteMemberConversion()
    {
        FTSTikFinityDecodedMemberMessage Message =
            MakeMinimalValidMemberMessage();
        Message.Data->ActionId = 73;

        FTSTikFinityDecodedUser& User = *Message.Data->User;
        User.UniqueId = "complete-member-user";
        User.Nickname = "Member User";
        User.ProfilePictureUrl = "https://example.test/member-user.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;

        const FTSTikFinityMemberConversionResult Result =
            FTSTikFinityMemberConverter::Convert(Message);
        RequireMemberConversionStatus(
            Result,
            ETSTikFinityMemberConversionStatus::Converted,
            "Complete Member conversion"
        );

        const FTSMemberInput& Input = *Result.Input;
        Require(Input.ActionId == 73, "Complete Member ActionId");
        Require(
            Input.User.UniqueId == "complete-member-user",
            "Complete Member UniqueId"
        );
        Require(Input.User.Nickname == "Member User", "Member Nickname");
        Require(
            Input.User.ProfilePictureUrl ==
                "https://example.test/member-user.png",
            "Member ProfilePictureUrl"
        );
        Require(Input.User.FollowRole == 3, "Member FollowRole");
        Require(Input.User.bIsModerator, "Member bIsModerator");
        Require(!Input.User.bIsSubscriber, "Member bIsSubscriber");
        Require(Input.User.bIsNewGifter, "Member bIsNewGifter");
        Require(Input.User.TopGifterRank == 7, "Member TopGifterRank");
        Require(Input.User.GifterLevel == 11, "Member GifterLevel");
        Require(
            Input.User.TeamMemberLevel == 13,
            "Member TeamMemberLevel"
        );
    }

    void TestMemberOptionalUserFieldDefaults()
    {
        const FTSTikFinityMemberConversionResult Result =
            FTSTikFinityMemberConverter::Convert(
                MakeMinimalValidMemberMessage()
            );
        RequireMemberConversionStatus(
            Result,
            ETSTikFinityMemberConversionStatus::Converted,
            "Member optional user defaults"
        );

        const FTSUserSnapshot& User = Result.Input->User;
        Require(User.UniqueId == "member-user-42", "Member default UniqueId");
        Require(User.Nickname.empty(), "Member default Nickname");
        Require(
            User.ProfilePictureUrl.empty(),
            "Member default ProfilePictureUrl"
        );
        Require(User.FollowRole == 0, "Member default FollowRole");
        Require(!User.bIsModerator, "Member default bIsModerator");
        Require(!User.bIsSubscriber, "Member default bIsSubscriber");
        Require(!User.bIsNewGifter, "Member default bIsNewGifter");
        Require(User.TopGifterRank == 0, "Member default TopGifterRank");
        Require(User.GifterLevel == 0, "Member default GifterLevel");
        Require(
            User.TeamMemberLevel == 0,
            "Member default TeamMemberLevel"
        );
    }

    void TestNonMemberEventIsIgnored()
    {
        FTSTikFinityDecodedMemberMessage Message;
        Message.EventName = "follow";
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Message),
            ETSTikFinityMemberConversionStatus::IgnoredNonMemberEvent,
            "Non-Member event"
        );
    }

    void TestInvalidMemberEnvelope()
    {
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert({}),
            ETSTikFinityMemberConversionStatus::RejectedInvalidEnvelope,
            "Empty Member envelope"
        );
    }

    void TestMissingMemberData()
    {
        FTSTikFinityDecodedMemberMessage Message;
        Message.EventName = "member";
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Message),
            ETSTikFinityMemberConversionStatus::RejectedMissingData,
            "Missing Member data"
        );
    }

    void TestMissingMemberUser()
    {
        FTSTikFinityDecodedMemberMessage Message;
        Message.EventName = "member";
        Message.Data.emplace();
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Message),
            ETSTikFinityMemberConversionStatus::RejectedMissingUser,
            "Missing Member user"
        );
    }

    void TestMissingOrEmptyMemberUserIdentity()
    {
        FTSTikFinityDecodedMemberMessage MissingIdentity =
            MakeMinimalValidMemberMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(MissingIdentity),
            ETSTikFinityMemberConversionStatus::RejectedMissingUserIdentity,
            "Missing Member identity"
        );

        FTSTikFinityDecodedMemberMessage EmptyIdentity =
            MakeMinimalValidMemberMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(EmptyIdentity),
            ETSTikFinityMemberConversionStatus::RejectedMissingUserIdentity,
            "Empty Member identity"
        );
    }

    void TestMissingMemberActionId()
    {
        FTSTikFinityDecodedMemberMessage Message =
            MakeMinimalValidMemberMessage();
        Message.Data->ActionId.reset();
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Message),
            ETSTikFinityMemberConversionStatus::RejectedMissingActionId,
            "Missing Member ActionId"
        );
    }

    void TestMemberActionIdRepresentationBoundaries()
    {
        FTSTikFinityDecodedMemberMessage Zero =
            MakeMinimalValidMemberMessage();
        Zero.Data->ActionId = 0;
        const FTSTikFinityMemberConversionResult ZeroResult =
            FTSTikFinityMemberConverter::Convert(Zero);
        RequireMemberConversionStatus(
            ZeroResult,
            ETSTikFinityMemberConversionStatus::Converted,
            "Member zero ActionId"
        );
        Require(ZeroResult.Input->ActionId == 0, "Member zero preservation");

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedMemberMessage Maximum =
            MakeMinimalValidMemberMessage();
        Maximum.Data->ActionId = Int32Max;
        const FTSTikFinityMemberConversionResult MaximumResult =
            FTSTikFinityMemberConverter::Convert(Maximum);
        RequireMemberConversionStatus(
            MaximumResult,
            ETSTikFinityMemberConversionStatus::Converted,
            "Member INT32_MAX ActionId"
        );
        Require(
            MaximumResult.Input->ActionId ==
                std::numeric_limits<std::int32_t>::max(),
            "Member INT32_MAX preservation"
        );

        FTSTikFinityDecodedMemberMessage Negative =
            MakeMinimalValidMemberMessage();
        Negative.Data->ActionId = -1;
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Negative),
            ETSTikFinityMemberConversionStatus::RejectedInvalidNumericField,
            "Negative Member ActionId"
        );

        FTSTikFinityDecodedMemberMessage AboveMaximum =
            MakeMinimalValidMemberMessage();
        AboveMaximum.Data->ActionId = Int32Max + 1;
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(AboveMaximum),
            ETSTikFinityMemberConversionStatus::RejectedInvalidNumericField,
            "Above-max Member ActionId"
        );
    }

    void TestInvalidMemberUserNumericField()
    {
        FTSTikFinityDecodedMemberMessage Message =
            MakeMinimalValidMemberMessage();
        Message.Data->User->GifterLevel =
            static_cast<std::int64_t>(
                std::numeric_limits<std::int32_t>::max()
            ) + 1;
        RequireMemberConversionStatus(
            FTSTikFinityMemberConverter::Convert(Message),
            ETSTikFinityMemberConversionStatus::RejectedInvalidNumericField,
            "Invalid Member user numeric field"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityMemberAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Complete Member conversion", &TestCompleteMemberConversion});
        Tests.push_back({"Member optional user field defaults", &TestMemberOptionalUserFieldDefaults});
        Tests.push_back({"Non-Member event is ignored", &TestNonMemberEventIsIgnored});
        Tests.push_back({"Invalid Member envelope", &TestInvalidMemberEnvelope});
        Tests.push_back({"Missing Member data", &TestMissingMemberData});
        Tests.push_back({"Missing Member user", &TestMissingMemberUser});
        Tests.push_back({"Missing or empty Member user identity", &TestMissingOrEmptyMemberUserIdentity});
        Tests.push_back({"Missing Member ActionId", &TestMissingMemberActionId});
        Tests.push_back({"Member ActionId representation boundaries", &TestMemberActionIdRepresentationBoundaries});
        Tests.push_back({"Invalid Member user numeric field", &TestInvalidMemberUserNumericField});
    }
}
