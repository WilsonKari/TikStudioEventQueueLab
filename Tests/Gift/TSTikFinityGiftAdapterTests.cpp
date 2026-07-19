#include "TikFinity/TSTikFinityGiftConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSTestSuites.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace
{
    using TikStudio::Tests::Require;

    [[nodiscard]]
    FTSTikFinityDecodedGiftMessage MakeMinimalValidGiftMessage()
    {
        FTSTikFinityDecodedGiftMessage Message;
        Message.EventName = "gift";
        Message.Data.emplace();
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "gift-user-42";
        Message.Data->GiftId = 1;
        Message.Data->GiftName = "Rose";
        Message.Data->DiamondCount = 1;
        return Message;
    }

    void RequireGiftConversionStatus(
        const FTSTikFinityGiftConversionResult& Result,
        ETSTikFinityGiftConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus ==
                    ETSTikFinityGiftConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

    void RequireCompleteGiftInput(
        const FTSGiftInput& Input,
        const std::string& Context
    )
    {
        Require(Input.GiftId == 5655, Context + ": GiftId");
        Require(Input.GiftName == "Rose", Context + ": GiftName");
        Require(
            Input.GiftPictureUrl == "https://example.test/gift.png",
            Context + ": GiftPictureUrl"
        );
        Require(Input.DiamondCount == 20, Context + ": DiamondCount");
        Require(Input.RepeatCount == 7, Context + ": RepeatCount");
        Require(Input.GiftType == 1, Context + ": GiftType");
        Require(
            Input.Describe == "Complete portable Gift",
            Context + ": Describe"
        );
        Require(Input.bRepeatEnd, Context + ": bRepeatEnd");
        Require(Input.GroupId == "gift-group-42", Context + ": GroupId");
        Require(
            Input.User.UniqueId == "complete-gift-user",
            Context + ": UniqueId"
        );
        Require(Input.User.Nickname == "Gift User", Context + ": Nickname");
        Require(
            Input.User.ProfilePictureUrl ==
                "https://example.test/gift-user.png",
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

    void TestCompleteGiftConversion()
    {
        FTSTikFinityDecodedGiftMessage Message =
            MakeMinimalValidGiftMessage();
        FTSTikFinityDecodedGiftData& Data = *Message.Data;
        Data.GiftId = 5655;
        Data.GiftName = "Rose";
        Data.GiftPictureUrl = "https://example.test/gift.png";
        Data.DiamondCount = 20;
        Data.RepeatCount = 7;
        Data.GiftType = 1;
        Data.Describe = "Complete portable Gift";
        Data.bRepeatEnd = true;
        Data.GroupId = "gift-group-42";

        FTSTikFinityDecodedUser& User = *Data.User;
        User.UniqueId = "complete-gift-user";
        User.Nickname = "Gift User";
        User.ProfilePictureUrl = "https://example.test/gift-user.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;

        const FTSTikFinityGiftConversionResult Result =
            FTSTikFinityGiftConverter::Convert(Message);
        RequireGiftConversionStatus(
            Result,
            ETSTikFinityGiftConversionStatus::Converted,
            "Complete Gift conversion"
        );
        RequireCompleteGiftInput(*Result.Input, "Complete Gift");
    }

    void TestGiftMinimalDefaults()
    {
        const FTSTikFinityGiftConversionResult Result =
            FTSTikFinityGiftConverter::Convert(
                MakeMinimalValidGiftMessage()
            );
        RequireGiftConversionStatus(
            Result,
            ETSTikFinityGiftConversionStatus::Converted,
            "Gift minimal defaults"
        );

        const FTSGiftInput& Input = *Result.Input;
        Require(Input.GiftId == 1, "Gift minimal GiftId");
        Require(Input.GiftName == "Rose", "Gift minimal GiftName");
        Require(Input.DiamondCount == 1, "Gift minimal DiamondCount");
        Require(Input.GiftPictureUrl.empty(), "Gift default picture URL");
        Require(Input.RepeatCount == 1, "Gift default RepeatCount");
        Require(Input.GiftType == 0, "Gift default GiftType");
        Require(Input.Describe.empty(), "Gift default Describe");
        Require(!Input.bRepeatEnd, "Gift default bRepeatEnd");
        Require(Input.GroupId.empty(), "Gift default GroupId");
    }

    void TestGiftOptionalUserFieldDefaults()
    {
        const FTSTikFinityGiftConversionResult Result =
            FTSTikFinityGiftConverter::Convert(
                MakeMinimalValidGiftMessage()
            );
        RequireGiftConversionStatus(
            Result,
            ETSTikFinityGiftConversionStatus::Converted,
            "Gift optional user defaults"
        );

        const FTSUserSnapshot& User = Result.Input->User;
        Require(User.UniqueId == "gift-user-42", "Gift default UniqueId");
        Require(User.Nickname.empty(), "Gift default Nickname");
        Require(
            User.ProfilePictureUrl.empty(),
            "Gift default ProfilePictureUrl"
        );
        Require(User.FollowRole == 0, "Gift default FollowRole");
        Require(!User.bIsModerator, "Gift default bIsModerator");
        Require(!User.bIsSubscriber, "Gift default bIsSubscriber");
        Require(!User.bIsNewGifter, "Gift default bIsNewGifter");
        Require(User.TopGifterRank == 0, "Gift default TopGifterRank");
        Require(User.GifterLevel == 0, "Gift default GifterLevel");
        Require(User.TeamMemberLevel == 0, "Gift default TeamMemberLevel");
    }

    void TestNonGiftEventIsIgnored()
    {
        FTSTikFinityDecodedGiftMessage Message;
        Message.EventName = "member";
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(Message),
            ETSTikFinityGiftConversionStatus::IgnoredNonGiftEvent,
            "Non-Gift event"
        );
    }

    void TestInvalidEnvelopeAndMissingGiftDataOrUser()
    {
        FTSTikFinityDecodedGiftMessage EmptyEnvelope;
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(EmptyEnvelope),
            ETSTikFinityGiftConversionStatus::RejectedInvalidEnvelope,
            "Empty Gift envelope"
        );

        FTSTikFinityDecodedGiftMessage MissingData;
        MissingData.EventName = "gift";
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingData),
            ETSTikFinityGiftConversionStatus::RejectedMissingData,
            "Missing Gift data"
        );

        FTSTikFinityDecodedGiftMessage MissingUser;
        MissingUser.EventName = "gift";
        MissingUser.Data.emplace();
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingUser),
            ETSTikFinityGiftConversionStatus::RejectedMissingUser,
            "Missing Gift user"
        );
    }

    void TestMissingOrEmptyGiftUserIdentity()
    {
        FTSTikFinityDecodedGiftMessage MissingIdentity =
            MakeMinimalValidGiftMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingIdentity),
            ETSTikFinityGiftConversionStatus::RejectedMissingUserIdentity,
            "Missing Gift identity"
        );

        FTSTikFinityDecodedGiftMessage EmptyIdentity =
            MakeMinimalValidGiftMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(EmptyIdentity),
            ETSTikFinityGiftConversionStatus::RejectedMissingUserIdentity,
            "Empty Gift identity"
        );
    }

    void TestGiftRequiredFieldValidation()
    {
        FTSTikFinityDecodedGiftMessage MissingGiftId =
            MakeMinimalValidGiftMessage();
        MissingGiftId.Data->GiftId.reset();
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingGiftId),
            ETSTikFinityGiftConversionStatus::RejectedMissingGiftId,
            "Missing GiftId"
        );

        FTSTikFinityDecodedGiftMessage MissingGiftName =
            MakeMinimalValidGiftMessage();
        MissingGiftName.Data->GiftName.reset();
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingGiftName),
            ETSTikFinityGiftConversionStatus::RejectedMissingGiftName,
            "Missing GiftName"
        );

        FTSTikFinityDecodedGiftMessage EmptyGiftName =
            MakeMinimalValidGiftMessage();
        EmptyGiftName.Data->GiftName = "";
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(EmptyGiftName),
            ETSTikFinityGiftConversionStatus::RejectedMissingGiftName,
            "Empty GiftName"
        );

        FTSTikFinityDecodedGiftMessage MissingDiamondCount =
            MakeMinimalValidGiftMessage();
        MissingDiamondCount.Data->DiamondCount.reset();
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(MissingDiamondCount),
            ETSTikFinityGiftConversionStatus::RejectedMissingDiamondCount,
            "Missing DiamondCount"
        );
    }

    void TestGiftNumericRepresentationBoundaries()
    {
        FTSTikFinityDecodedGiftMessage Zero = MakeMinimalValidGiftMessage();
        Zero.Data->GiftId = 0;
        Zero.Data->DiamondCount = 0;
        Zero.Data->RepeatCount = 0;
        Zero.Data->GiftType = 0;
        Zero.Data->User->FollowRole = 0;
        Zero.Data->User->TopGifterRank = 0;
        Zero.Data->User->GifterLevel = 0;
        Zero.Data->User->TeamMemberLevel = 0;
        const FTSTikFinityGiftConversionResult ZeroResult =
            FTSTikFinityGiftConverter::Convert(Zero);
        RequireGiftConversionStatus(
            ZeroResult,
            ETSTikFinityGiftConversionStatus::Converted,
            "Gift numeric zero"
        );
        Require(
            ZeroResult.Input->GiftId == 0 &&
                ZeroResult.Input->DiamondCount == 0 &&
                ZeroResult.Input->RepeatCount == 0 &&
                ZeroResult.Input->GiftType == 0 &&
                ZeroResult.Input->User.FollowRole == 0 &&
                ZeroResult.Input->User.TopGifterRank == 0 &&
                ZeroResult.Input->User.GifterLevel == 0 &&
                ZeroResult.Input->User.TeamMemberLevel == 0,
            "Gift numeric zeros must be preserved"
        );

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedGiftMessage Maximum =
            MakeMinimalValidGiftMessage();
        Maximum.Data->GiftId = Int32Max;
        Maximum.Data->DiamondCount = Int32Max;
        Maximum.Data->RepeatCount = Int32Max;
        Maximum.Data->GiftType = Int32Max;
        Maximum.Data->User->FollowRole = Int32Max;
        Maximum.Data->User->TopGifterRank = Int32Max;
        Maximum.Data->User->GifterLevel = Int32Max;
        Maximum.Data->User->TeamMemberLevel = Int32Max;
        const FTSTikFinityGiftConversionResult MaximumResult =
            FTSTikFinityGiftConverter::Convert(Maximum);
        RequireGiftConversionStatus(
            MaximumResult,
            ETSTikFinityGiftConversionStatus::Converted,
            "Gift numeric INT32_MAX"
        );
        Require(
            MaximumResult.Input->GiftId ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->DiamondCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->RepeatCount ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->GiftType ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.FollowRole ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TopGifterRank ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.GifterLevel ==
                    std::numeric_limits<std::int32_t>::max() &&
                MaximumResult.Input->User.TeamMemberLevel ==
                    std::numeric_limits<std::int32_t>::max(),
            "Gift numeric INT32_MAX values"
        );

        const std::int64_t AboveInt32Max = Int32Max + 1;
        struct FNumericCase
        {
            const char* Context;
            void (*Mutate)(FTSTikFinityDecodedGiftMessage&, std::int64_t);
        };
        const FNumericCase GiftCases[]{
            {"GiftId", [](FTSTikFinityDecodedGiftMessage& Message, std::int64_t Value)
                { Message.Data->GiftId = Value; }},
            {"DiamondCount", [](FTSTikFinityDecodedGiftMessage& Message, std::int64_t Value)
                { Message.Data->DiamondCount = Value; }},
            {"RepeatCount", [](FTSTikFinityDecodedGiftMessage& Message, std::int64_t Value)
                { Message.Data->RepeatCount = Value; }},
            {"GiftType", [](FTSTikFinityDecodedGiftMessage& Message, std::int64_t Value)
                { Message.Data->GiftType = Value; }}
        };

        for (const FNumericCase& Case : GiftCases)
        {
            FTSTikFinityDecodedGiftMessage Negative =
                MakeMinimalValidGiftMessage();
            Case.Mutate(Negative, -1);
            RequireGiftConversionStatus(
                FTSTikFinityGiftConverter::Convert(Negative),
                ETSTikFinityGiftConversionStatus::RejectedInvalidNumericField,
                std::string{"Negative "} + Case.Context
            );

            FTSTikFinityDecodedGiftMessage AboveMaximum =
                MakeMinimalValidGiftMessage();
            Case.Mutate(AboveMaximum, AboveInt32Max);
            RequireGiftConversionStatus(
                FTSTikFinityGiftConverter::Convert(AboveMaximum),
                ETSTikFinityGiftConversionStatus::RejectedInvalidNumericField,
                std::string{"Above-max "} + Case.Context
            );
        }

        FTSTikFinityDecodedGiftMessage InvalidUserNumeric =
            MakeMinimalValidGiftMessage();
        InvalidUserNumeric.Data->User->GifterLevel = AboveInt32Max;
        RequireGiftConversionStatus(
            FTSTikFinityGiftConverter::Convert(InvalidUserNumeric),
            ETSTikFinityGiftConversionStatus::RejectedInvalidNumericField,
            "Above-max Gift user numeric"
        );
    }

    void TestGiftRepeatMetadataHasNoInferredComboSemantics()
    {
        FTSTikFinityDecodedGiftMessage CaseA = MakeMinimalValidGiftMessage();
        CaseA.Data->GiftName = "  Spaced Gift  ";
        CaseA.Data->RepeatCount = 0;
        CaseA.Data->GiftType = 77;
        CaseA.Data->bRepeatEnd = false;
        CaseA.Data->GroupId = "";
        const FTSTikFinityGiftConversionResult ResultA =
            FTSTikFinityGiftConverter::Convert(CaseA);
        RequireGiftConversionStatus(
            ResultA,
            ETSTikFinityGiftConversionStatus::Converted,
            "Independent Gift repeat metadata A"
        );
        Require(
            ResultA.Input->GiftName == "  Spaced Gift  " &&
                ResultA.Input->RepeatCount == 0 &&
                ResultA.Input->GiftType == 77 &&
                !ResultA.Input->bRepeatEnd &&
                ResultA.Input->GroupId.empty(),
            "Gift repeat metadata A must be preserved exactly"
        );

        FTSTikFinityDecodedGiftMessage CaseB = MakeMinimalValidGiftMessage();
        CaseB.Data->RepeatCount = 250;
        CaseB.Data->GiftType = 0;
        CaseB.Data->bRepeatEnd = true;
        CaseB.Data->GroupId = "arbitrary-group";
        const FTSTikFinityGiftConversionResult ResultB =
            FTSTikFinityGiftConverter::Convert(CaseB);
        RequireGiftConversionStatus(
            ResultB,
            ETSTikFinityGiftConversionStatus::Converted,
            "Independent Gift repeat metadata B"
        );
        Require(
            ResultB.Input->RepeatCount == 250 &&
                ResultB.Input->GiftType == 0 &&
                ResultB.Input->bRepeatEnd &&
                ResultB.Input->GroupId == "arbitrary-group",
            "Gift repeat metadata B must be preserved exactly"
        );
    }

    void TestJsonGiftDecodeAndConverterIntegration()
    {
        constexpr const char* Json = R"json(
{
  "event": "gift",
  "data": {
    "giftId": 10,
    "giftName": "Rose",
    "giftPictureUrl": "gift-url",
    "diamondCount": 20,
    "repeatCount": 30,
    "giftType": 40,
    "describe": "desc",
    "repeatEnd": true,
    "groupId": "group",
    "uniqueId": "gift-json-user",
    "nickname": "Gift User",
    "profilePictureUrl": "user-url",
    "followRole": 1,
    "isModerator": true,
    "isSubscriber": false,
    "isNewGifter": true,
    "topGifterRank": 2,
    "gifterLevel": 3,
    "teamMemberLevel": 4
  }
}
)json";

        const FTSTikFinityJsonDecodeResult Decoded =
            FTSTikFinityJsonEventDecoder::Decode(Json);
        Require(
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded &&
                Decoded.Event.has_value(),
            "Gift JSON must decode"
        );

        const FTSTikFinityDecodedGiftMessage* GiftMessage =
            std::get_if<FTSTikFinityDecodedGiftMessage>(&*Decoded.Event);
        Require(GiftMessage != nullptr, "Decoded variant must contain Gift");

        const FTSTikFinityGiftConversionResult Converted =
            FTSTikFinityGiftConverter::Convert(*GiftMessage);
        RequireGiftConversionStatus(
            Converted,
            ETSTikFinityGiftConversionStatus::Converted,
            "Decoded Gift conversion"
        );

        const FTSGiftInput& Input = *Converted.Input;
        Require(Input.GiftId == 10, "Decoded GiftId");
        Require(Input.GiftName == "Rose", "Decoded GiftName");
        Require(Input.GiftPictureUrl == "gift-url", "Decoded GiftPictureUrl");
        Require(Input.DiamondCount == 20, "Decoded DiamondCount");
        Require(Input.RepeatCount == 30, "Decoded RepeatCount");
        Require(Input.GiftType == 40, "Decoded GiftType");
        Require(Input.Describe == "desc", "Decoded Describe");
        Require(Input.bRepeatEnd, "Decoded bRepeatEnd");
        Require(Input.GroupId == "group", "Decoded GroupId");
        Require(Input.User.UniqueId == "gift-json-user", "Decoded UniqueId");
        Require(Input.User.Nickname == "Gift User", "Decoded Nickname");
        Require(
            Input.User.ProfilePictureUrl == "user-url",
            "Decoded ProfilePictureUrl"
        );
        Require(Input.User.FollowRole == 1, "Decoded FollowRole");
        Require(Input.User.bIsModerator, "Decoded bIsModerator");
        Require(!Input.User.bIsSubscriber, "Decoded bIsSubscriber");
        Require(Input.User.bIsNewGifter, "Decoded bIsNewGifter");
        Require(Input.User.TopGifterRank == 2, "Decoded TopGifterRank");
        Require(Input.User.GifterLevel == 3, "Decoded GifterLevel");
        Require(Input.User.TeamMemberLevel == 4, "Decoded TeamMemberLevel");
    }
}

namespace TikStudio::Tests
{
    void RegisterTikFinityGiftAdapterTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Complete Gift conversion", &TestCompleteGiftConversion});
        Tests.push_back({"Gift minimal defaults", &TestGiftMinimalDefaults});
        Tests.push_back({"Gift optional user field defaults", &TestGiftOptionalUserFieldDefaults});
        Tests.push_back({"Non-Gift event is ignored", &TestNonGiftEventIsIgnored});
        Tests.push_back({"Invalid envelope and missing Gift data or user", &TestInvalidEnvelopeAndMissingGiftDataOrUser});
        Tests.push_back({"Missing or empty Gift user identity", &TestMissingOrEmptyGiftUserIdentity});
        Tests.push_back({"Gift required field validation", &TestGiftRequiredFieldValidation});
        Tests.push_back({"Gift numeric representation boundaries", &TestGiftNumericRepresentationBoundaries});
        Tests.push_back({"Gift repeat metadata has no inferred combo semantics", &TestGiftRepeatMetadataHasNoInferredComboSemantics});
        Tests.push_back({"JSON Gift decode and converter integration", &TestJsonGiftDecodeAndConverterIntegration});
    }
}
