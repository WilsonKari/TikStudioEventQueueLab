#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityLikeConverter.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedLikeMessage MakeMinimalValidLikeMessage()
    {
        FTSTikFinityDecodedLikeMessage Message;
        Message.EventName = "like";
        Message.Data.emplace();
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "like-user-42";
        Message.Data->LikeCount = 1;
        Message.Data->TotalLikeCount = 1;
        return Message;
    }

    void RequireLikeConversionStatus(
        const FTSTikFinityLikeConversionResult& Result,
        ETSTikFinityLikeConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityLikeConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void RequireCompleteLikeInput(
        const FTSLikeInput& Input,
        const std::string& UniqueId,
        const std::string& Context
    )
    {
        Require(Input.LikeCount == 5, Context + ": LikeCount");
        Require(Input.TotalLikeCount == 50, Context + ": TotalLikeCount");
        Require(Input.User.UniqueId == UniqueId, Context + ": UniqueId");
        Require(Input.User.Nickname == "Like User", Context + ": Nickname");
        Require(
            Input.User.ProfilePictureUrl ==
                "https://example.test/like.png",
            Context + ": ProfilePictureUrl"
        );
        Require(Input.User.FollowRole == 3, Context + ": FollowRole");
        Require(Input.User.bIsModerator, Context + ": bIsModerator");
        Require(!Input.User.bIsSubscriber, Context + ": bIsSubscriber");
        Require(Input.User.bIsNewGifter, Context + ": bIsNewGifter");
        Require(Input.User.TopGifterRank == 7, Context + ": TopGifterRank");
        Require(Input.User.GifterLevel == 11, Context + ": GifterLevel");
        Require(
            Input.User.TeamMemberLevel == 13,
            Context + ": TeamMemberLevel"
        );
    }

    void TestCompleteLikeConversion()
    {
        FTSTikFinityDecodedLikeMessage Message =
            MakeMinimalValidLikeMessage();
        Message.Data->LikeCount = 5;
        Message.Data->TotalLikeCount = 50;
        FTSTikFinityDecodedUser& User = *Message.Data->User;
        User.UniqueId = "complete-like-user";
        User.Nickname = "Like User";
        User.ProfilePictureUrl = "https://example.test/like.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;

        const FTSTikFinityLikeConversionResult Result =
            FTSTikFinityLikeConverter::Convert(Message);
        RequireLikeConversionStatus(
            Result,
            ETSTikFinityLikeConversionStatus::Converted,
            "Complete Like conversion"
        );
        RequireCompleteLikeInput(
            *Result.Input,
            "complete-like-user",
            "Complete Like"
        );
    }

    void TestLikeOptionalUserFieldDefaults()
    {
        const FTSTikFinityLikeConversionResult Result =
            FTSTikFinityLikeConverter::Convert(
                MakeMinimalValidLikeMessage()
            );
        RequireLikeConversionStatus(
            Result,
            ETSTikFinityLikeConversionStatus::Converted,
            "Like optional defaults"
        );

        const FTSLikeInput& Input = *Result.Input;
        Require(Input.LikeCount == 1, "Like default LikeCount");
        Require(Input.TotalLikeCount == 1, "Like default TotalLikeCount");
        Require(Input.User.UniqueId == "like-user-42", "Like default UniqueId");
        Require(Input.User.Nickname.empty(), "Like default Nickname");
        Require(
            Input.User.ProfilePictureUrl.empty(),
            "Like default ProfilePictureUrl"
        );
        Require(Input.User.FollowRole == 0, "Like default FollowRole");
        Require(!Input.User.bIsModerator, "Like default bIsModerator");
        Require(!Input.User.bIsSubscriber, "Like default bIsSubscriber");
        Require(!Input.User.bIsNewGifter, "Like default bIsNewGifter");
        Require(Input.User.TopGifterRank == 0, "Like default TopGifterRank");
        Require(Input.User.GifterLevel == 0, "Like default GifterLevel");
        Require(
            Input.User.TeamMemberLevel == 0,
            "Like default TeamMemberLevel"
        );
    }

    void TestNonLikeEventIsIgnored()
    {
        FTSTikFinityDecodedLikeMessage Message;
        Message.EventName = "share";
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(Message),
            ETSTikFinityLikeConversionStatus::IgnoredNonLikeEvent,
            "Non-Like event"
        );
    }

    void TestInvalidEnvelopeAndMissingLikeDataOrUser()
    {
        FTSTikFinityDecodedLikeMessage EmptyEnvelope;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(EmptyEnvelope),
            ETSTikFinityLikeConversionStatus::RejectedInvalidEnvelope,
            "Empty Like envelope"
        );

        FTSTikFinityDecodedLikeMessage MissingData;
        MissingData.EventName = "like";
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(MissingData),
            ETSTikFinityLikeConversionStatus::RejectedMissingData,
            "Missing Like data"
        );

        FTSTikFinityDecodedLikeMessage MissingUser;
        MissingUser.EventName = "like";
        MissingUser.Data.emplace();
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(MissingUser),
            ETSTikFinityLikeConversionStatus::RejectedMissingUser,
            "Missing Like user"
        );
    }

    void TestMissingOrEmptyLikeIdentity()
    {
        FTSTikFinityDecodedLikeMessage MissingIdentity =
            MakeMinimalValidLikeMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(MissingIdentity),
            ETSTikFinityLikeConversionStatus::RejectedMissingUserIdentity,
            "Missing Like identity"
        );

        FTSTikFinityDecodedLikeMessage EmptyIdentity =
            MakeMinimalValidLikeMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(EmptyIdentity),
            ETSTikFinityLikeConversionStatus::RejectedMissingUserIdentity,
            "Empty Like identity"
        );
    }

    void TestMissingLikeCounters()
    {
        FTSTikFinityDecodedLikeMessage MissingLikeCount =
            MakeMinimalValidLikeMessage();
        MissingLikeCount.Data->LikeCount.reset();
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(MissingLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedMissingLikeCount,
            "Missing LikeCount"
        );

        FTSTikFinityDecodedLikeMessage MissingTotalLikeCount =
            MakeMinimalValidLikeMessage();
        MissingTotalLikeCount.Data->TotalLikeCount.reset();
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(MissingTotalLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedMissingTotalLikeCount,
            "Missing TotalLikeCount"
        );
    }

    void TestLikeNumericRepresentationBoundaries()
    {
        FTSTikFinityDecodedLikeMessage Zero =
            MakeMinimalValidLikeMessage();
        Zero.Data->LikeCount = 0;
        Zero.Data->TotalLikeCount = 0;
        const FTSTikFinityLikeConversionResult ZeroResult =
            FTSTikFinityLikeConverter::Convert(Zero);
        RequireLikeConversionStatus(
            ZeroResult,
            ETSTikFinityLikeConversionStatus::Converted,
            "Like numeric zero"
        );
        Require(
            ZeroResult.Input->LikeCount == 0 &&
                ZeroResult.Input->TotalLikeCount == 0,
            "Like zero counters must be preserved"
        );

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedLikeMessage Maximum =
            MakeMinimalValidLikeMessage();
        Maximum.Data->LikeCount = Int32Max;
        Maximum.Data->TotalLikeCount = Int32Max;
        Maximum.Data->User->FollowRole = Int32Max;
        Maximum.Data->User->TopGifterRank = Int32Max;
        Maximum.Data->User->GifterLevel = Int32Max;
        Maximum.Data->User->TeamMemberLevel = Int32Max;
        const FTSTikFinityLikeConversionResult MaximumResult =
            FTSTikFinityLikeConverter::Convert(Maximum);
        RequireLikeConversionStatus(
            MaximumResult,
            ETSTikFinityLikeConversionStatus::Converted,
            "Like numeric INT32_MAX"
        );
        Require(
            MaximumResult.Input->LikeCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->TotalLikeCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.FollowRole ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TopGifterRank ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.GifterLevel ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TeamMemberLevel ==
                    std::numeric_limits<std::int32_t>::max(),
            "Like numeric INT32_MAX values"
        );

        FTSTikFinityDecodedLikeMessage IndependentCounters =
            MakeMinimalValidLikeMessage();
        IndependentCounters.Data->LikeCount = 20;
        IndependentCounters.Data->TotalLikeCount = 10;
        const FTSTikFinityLikeConversionResult IndependentResult =
            FTSTikFinityLikeConverter::Convert(IndependentCounters);
        RequireLikeConversionStatus(
            IndependentResult,
            ETSTikFinityLikeConversionStatus::Converted,
            "Independent Like counters"
        );
        Require(
            IndependentResult.Input->LikeCount == 20 &&
                IndependentResult.Input->TotalLikeCount == 10,
            "LikeCount may exceed TotalLikeCount without inferred semantics"
        );

        FTSTikFinityDecodedLikeMessage NegativeLikeCount =
            MakeMinimalValidLikeMessage();
        NegativeLikeCount.Data->LikeCount = -1;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(NegativeLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField,
            "Negative LikeCount"
        );

        FTSTikFinityDecodedLikeMessage NegativeTotalLikeCount =
            MakeMinimalValidLikeMessage();
        NegativeTotalLikeCount.Data->TotalLikeCount = -1;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(NegativeTotalLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField,
            "Negative TotalLikeCount"
        );

        FTSTikFinityDecodedLikeMessage AboveMaximumLikeCount =
            MakeMinimalValidLikeMessage();
        AboveMaximumLikeCount.Data->LikeCount = Int32Max + 1;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(AboveMaximumLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField,
            "Above-max LikeCount"
        );

        FTSTikFinityDecodedLikeMessage AboveMaximumTotalLikeCount =
            MakeMinimalValidLikeMessage();
        AboveMaximumTotalLikeCount.Data->TotalLikeCount = Int32Max + 1;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(AboveMaximumTotalLikeCount),
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField,
            "Above-max TotalLikeCount"
        );

        FTSTikFinityDecodedLikeMessage AboveMaximumUserField =
            MakeMinimalValidLikeMessage();
        AboveMaximumUserField.Data->User->GifterLevel = Int32Max + 1;
        RequireLikeConversionStatus(
            FTSTikFinityLikeConverter::Convert(AboveMaximumUserField),
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField,
            "Above-max Like user numeric"
        );
    }

    void TestJsonLikeDecodeAndConverterIntegration()
    {
        constexpr const char* Json = R"json(
{
  "event": "like",
  "data": {
    "likeCount": 5,
    "totalLikeCount": 50,
    "uniqueId": "json-like-user",
    "nickname": "Like User",
    "profilePictureUrl": "https://example.test/like.png",
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
            "Like JSON must decode"
        );

        const FTSTikFinityDecodedLikeMessage* LikeMessage =
            std::get_if<FTSTikFinityDecodedLikeMessage>(&*Decoded.Event);
        Require(LikeMessage != nullptr, "Decoded variant must contain Like");

        const FTSTikFinityLikeConversionResult Converted =
            FTSTikFinityLikeConverter::Convert(*LikeMessage);
        RequireLikeConversionStatus(
            Converted,
            ETSTikFinityLikeConversionStatus::Converted,
            "Decoded Like conversion"
        );
        RequireCompleteLikeInput(
            *Converted.Input,
            "json-like-user",
            "Decoded Like input"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityLikeAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Complete Like conversion", &TestCompleteLikeConversion});
        Tests.push_back({"Like optional user field defaults", &TestLikeOptionalUserFieldDefaults});
        Tests.push_back({"Non-Like event is ignored", &TestNonLikeEventIsIgnored});
        Tests.push_back({"Invalid envelope and missing Like data or user", &TestInvalidEnvelopeAndMissingLikeDataOrUser});
        Tests.push_back({"Missing or empty Like identity", &TestMissingOrEmptyLikeIdentity});
        Tests.push_back({"Missing Like counters", &TestMissingLikeCounters});
        Tests.push_back({"Like numeric representation boundaries", &TestLikeNumericRepresentationBoundaries});
        Tests.push_back({"JSON Like decode and converter integration", &TestJsonLikeDecodeAndConverterIntegration});
    }
}
