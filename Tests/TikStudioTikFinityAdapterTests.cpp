#include "TikFinity/TSTikFinityChatConverter.h"
#include "TikFinity/TSTikFinityFollowConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
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
    FTSTikFinityDecodedEmote MakeValidEmote(
        const std::string& Id,
        const std::string& Url
    )
    {
        FTSTikFinityDecodedEmote Emote;
        Emote.EmoteId = Id;
        Emote.EmoteImageUrl = Url;
        return Emote;
    }

    [[nodiscard]]
    FTSTikFinityDecodedChatMessage MakeMinimalValidMessage()
    {
        FTSTikFinityDecodedChatMessage Message;
        Message.EventName = "chat";
        Message.Data.emplace();
        Message.Data->Comment = "valid comment";
        Message.Data->User.emplace();
        Message.Data->User->UniqueId = "user-42";
        return Message;
    }

    void RequireConversionStatus(
        const FTSTikFinityChatConversionResult& Result,
        ETSTikFinityChatConversionStatus ExpectedStatus,
        const std::string& Context
    )
    {
        Require(Result.Status == ExpectedStatus, Context + ": status");
        Require(
            Result.Input.has_value() ==
                (ExpectedStatus == ETSTikFinityChatConversionStatus::Converted),
            Context + ": Input invariant"
        );
    }

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

    void RequireDecodedMessageEqual(
        const FTSTikFinityDecodedChatMessage& Actual,
        const FTSTikFinityDecodedChatMessage& Expected,
        const std::string& Context
    )
    {
        Require(Actual.EventName == Expected.EventName, Context + ": EventName");
        Require(
            Actual.Data.has_value() == Expected.Data.has_value(),
            Context + ": Data presence"
        );

        if (!Expected.Data.has_value())
        {
            return;
        }

        Require(
            Actual.Data->Comment == Expected.Data->Comment,
            Context + ": Comment"
        );
        Require(
            Actual.Data->Emotes.size() == Expected.Data->Emotes.size(),
            Context + ": Emote count"
        );

        for (std::size_t Index = 0;
             Index < Expected.Data->Emotes.size();
             ++Index)
        {
            Require(
                Actual.Data->Emotes[Index].EmoteId ==
                    Expected.Data->Emotes[Index].EmoteId,
                Context + ": EmoteId"
            );
            Require(
                Actual.Data->Emotes[Index].EmoteImageUrl ==
                    Expected.Data->Emotes[Index].EmoteImageUrl,
                Context + ": EmoteImageUrl"
            );
        }

        Require(
            Actual.Data->User.has_value() == Expected.Data->User.has_value(),
            Context + ": User presence"
        );

        if (!Expected.Data->User.has_value())
        {
            return;
        }

        const FTSTikFinityDecodedUser& ActualUser = *Actual.Data->User;
        const FTSTikFinityDecodedUser& ExpectedUser = *Expected.Data->User;
        Require(ActualUser.UniqueId == ExpectedUser.UniqueId, Context + ": UniqueId");
        Require(ActualUser.Nickname == ExpectedUser.Nickname, Context + ": Nickname");
        Require(
            ActualUser.ProfilePictureUrl == ExpectedUser.ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(
            ActualUser.FollowRole == ExpectedUser.FollowRole,
            Context + ": FollowRole"
        );
        Require(
            ActualUser.bIsModerator == ExpectedUser.bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            ActualUser.bIsSubscriber == ExpectedUser.bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(
            ActualUser.bIsNewGifter == ExpectedUser.bIsNewGifter,
            Context + ": bIsNewGifter"
        );
        Require(
            ActualUser.TopGifterRank == ExpectedUser.TopGifterRank,
            Context + ": TopGifterRank"
        );
        Require(
            ActualUser.GifterLevel == ExpectedUser.GifterLevel,
            Context + ": GifterLevel"
        );
        Require(
            ActualUser.TeamMemberLevel == ExpectedUser.TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    void TestCompleteConversion()
    {
        FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
        Message.Data->Comment = "Complete TikFinity comment";
        Message.Data->Emotes = {
            MakeValidEmote("emote-a", "https://example.test/a.png"),
            MakeValidEmote("emote-b", "https://example.test/b.png")
        };
        FTSTikFinityDecodedUser& User = *Message.Data->User;
        User.UniqueId = "complete-user";
        User.Nickname = "Complete User";
        User.ProfilePictureUrl = "https://example.test/user.png";
        User.FollowRole = 3;
        User.bIsModerator = true;
        User.bIsSubscriber = false;
        User.bIsNewGifter = true;
        User.TopGifterRank = 7;
        User.GifterLevel = 11;
        User.TeamMemberLevel = 13;
        const FTSTikFinityDecodedChatMessage Original = Message;

        const FTSTikFinityChatConversionResult Result =
            FTSTikFinityChatConverter::Convert(Message);
        RequireConversionStatus(
            Result,
            ETSTikFinityChatConversionStatus::Converted,
            "Complete conversion"
        );

        const FTSChatInput& Input = *Result.Input;
        Require(Input.Comment == "Complete TikFinity comment", "Complete Comment");
        Require(Input.Emotes.size() == 2, "Complete emote count");
        Require(Input.Emotes[0].EmoteId == "emote-a", "Complete first EmoteId");
        Require(
            Input.Emotes[0].EmoteImageUrl == "https://example.test/a.png",
            "Complete first EmoteImageUrl"
        );
        Require(Input.Emotes[1].EmoteId == "emote-b", "Complete second EmoteId");
        Require(
            Input.Emotes[1].EmoteImageUrl == "https://example.test/b.png",
            "Complete second EmoteImageUrl"
        );
        Require(Input.User.UniqueId == "complete-user", "Complete UniqueId");
        Require(Input.User.Nickname == "Complete User", "Complete Nickname");
        Require(
            Input.User.ProfilePictureUrl == "https://example.test/user.png",
            "Complete ProfilePictureUrl"
        );
        Require(Input.User.FollowRole == 3, "Complete FollowRole");
        Require(Input.User.bIsModerator, "Complete bIsModerator");
        Require(!Input.User.bIsSubscriber, "Complete bIsSubscriber");
        Require(Input.User.bIsNewGifter, "Complete bIsNewGifter");
        Require(Input.User.TopGifterRank == 7, "Complete TopGifterRank");
        Require(Input.User.GifterLevel == 11, "Complete GifterLevel");
        Require(Input.User.TeamMemberLevel == 13, "Complete TeamMemberLevel");
        RequireDecodedMessageEqual(Message, Original, "Original decoded message");
    }

    void TestOptionalFieldDefaults()
    {
        const FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
        const FTSTikFinityChatConversionResult Result =
            FTSTikFinityChatConverter::Convert(Message);
        RequireConversionStatus(
            Result,
            ETSTikFinityChatConversionStatus::Converted,
            "Optional defaults"
        );

        const FTSUserSnapshot& User = Result.Input->User;
        Require(User.UniqueId == "user-42", "Defaults UniqueId");
        Require(User.Nickname.empty(), "Defaults Nickname");
        Require(User.ProfilePictureUrl.empty(), "Defaults ProfilePictureUrl");
        Require(User.FollowRole == 0, "Defaults FollowRole");
        Require(!User.bIsModerator, "Defaults bIsModerator");
        Require(!User.bIsSubscriber, "Defaults bIsSubscriber");
        Require(!User.bIsNewGifter, "Defaults bIsNewGifter");
        Require(User.TopGifterRank == 0, "Defaults TopGifterRank");
        Require(User.GifterLevel == 0, "Defaults GifterLevel");
        Require(User.TeamMemberLevel == 0, "Defaults TeamMemberLevel");
    }

    void TestNonChatEventIsIgnoredWithoutChatInspection()
    {
        FTSTikFinityDecodedChatMessage Message;
        Message.EventName = "gift";

        const FTSTikFinityChatConversionResult Result =
            FTSTikFinityChatConverter::Convert(Message);
        RequireConversionStatus(
            Result,
            ETSTikFinityChatConversionStatus::IgnoredNonChatEvent,
            "Non-Chat event"
        );
    }

    void TestInvalidEnvelopeAndMissingDataOrUser()
    {
        FTSTikFinityDecodedChatMessage EmptyEnvelope;
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(EmptyEnvelope),
            ETSTikFinityChatConversionStatus::RejectedInvalidEnvelope,
            "Empty envelope"
        );

        FTSTikFinityDecodedChatMessage MissingData;
        MissingData.EventName = "chat";
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(MissingData),
            ETSTikFinityChatConversionStatus::RejectedMissingData,
            "Missing data"
        );

        FTSTikFinityDecodedChatMessage MissingUser;
        MissingUser.EventName = "chat";
        MissingUser.Data.emplace();
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(MissingUser),
            ETSTikFinityChatConversionStatus::RejectedMissingUser,
            "Missing user"
        );
    }

    void TestMissingOrEmptyUserIdentity()
    {
        FTSTikFinityDecodedChatMessage MissingIdentity =
            MakeMinimalValidMessage();
        MissingIdentity.Data->User->UniqueId.reset();
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(MissingIdentity),
            ETSTikFinityChatConversionStatus::RejectedMissingUserIdentity,
            "Missing identity"
        );

        FTSTikFinityDecodedChatMessage EmptyIdentity =
            MakeMinimalValidMessage();
        EmptyIdentity.Data->User->UniqueId = "";
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(EmptyIdentity),
            ETSTikFinityChatConversionStatus::RejectedMissingUserIdentity,
            "Empty identity"
        );
    }

    void TestEmoteOnlyChatIsConverted()
    {
        FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
        Message.Data->Comment.reset();
        Message.Data->Emotes = {
            MakeValidEmote("only-emote", "https://example.test/only.png")
        };

        const FTSTikFinityChatConversionResult Result =
            FTSTikFinityChatConverter::Convert(Message);
        RequireConversionStatus(
            Result,
            ETSTikFinityChatConversionStatus::Converted,
            "Emote-only Chat"
        );
        Require(Result.Input->Comment.empty(), "Emote-only normalized Comment");
        Require(Result.Input->Emotes.size() == 1, "Emote-only count");
        Require(
            Result.Input->Emotes[0].EmoteId == "only-emote",
            "Emote-only ID"
        );
    }

    void TestEmptyContentIsRejected()
    {
        FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
        Message.Data->Comment.reset();
        Message.Data->Emotes.clear();

        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(Message),
            ETSTikFinityChatConversionStatus::RejectedEmptyContent,
            "Empty content"
        );
    }

    void TestInvalidEmoteVariantsRejectWholeMessage()
    {
        FTSTikFinityDecodedEmote MissingId;
        MissingId.EmoteImageUrl = "https://example.test/emote.png";

        FTSTikFinityDecodedEmote EmptyId = MissingId;
        EmptyId.EmoteId = "";

        FTSTikFinityDecodedEmote MissingUrl;
        MissingUrl.EmoteId = "emote";

        FTSTikFinityDecodedEmote EmptyUrl = MissingUrl;
        EmptyUrl.EmoteImageUrl = "";

        const std::vector<FTSTikFinityDecodedEmote> InvalidEmotes{
            MissingId,
            EmptyId,
            MissingUrl,
            EmptyUrl
        };

        for (std::size_t Index = 0; Index < InvalidEmotes.size(); ++Index)
        {
            FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
            Message.Data->Emotes = {InvalidEmotes[Index]};
            RequireConversionStatus(
                FTSTikFinityChatConverter::Convert(Message),
                ETSTikFinityChatConversionStatus::RejectedInvalidEmote,
                "Invalid emote variant " + std::to_string(Index)
            );
        }
    }

    void TestNumericRepresentationBoundaries()
    {
        FTSTikFinityDecodedChatMessage Zero = MakeMinimalValidMessage();
        Zero.Data->User->FollowRole = 0;
        Zero.Data->User->TopGifterRank = 0;
        Zero.Data->User->GifterLevel = 0;
        Zero.Data->User->TeamMemberLevel = 0;
        const FTSTikFinityChatConversionResult ZeroResult =
            FTSTikFinityChatConverter::Convert(Zero);
        RequireConversionStatus(
            ZeroResult,
            ETSTikFinityChatConversionStatus::Converted,
            "Numeric zero"
        );
        Require(ZeroResult.Input->User.FollowRole == 0, "Numeric zero value");

        const std::int64_t Int32Max =
            std::numeric_limits<std::int32_t>::max();
        FTSTikFinityDecodedChatMessage Maximum = MakeMinimalValidMessage();
        Maximum.Data->User->FollowRole = Int32Max;
        Maximum.Data->User->TopGifterRank = Int32Max;
        Maximum.Data->User->GifterLevel = Int32Max;
        Maximum.Data->User->TeamMemberLevel = Int32Max;
        const FTSTikFinityChatConversionResult MaximumResult =
            FTSTikFinityChatConverter::Convert(Maximum);
        RequireConversionStatus(
            MaximumResult,
            ETSTikFinityChatConversionStatus::Converted,
            "Numeric INT32_MAX"
        );
        Require(
            MaximumResult.Input->User.TeamMemberLevel ==
                std::numeric_limits<std::int32_t>::max(),
            "Numeric INT32_MAX value"
        );

        FTSTikFinityDecodedChatMessage Negative = MakeMinimalValidMessage();
        Negative.Data->User->FollowRole = -1;
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(Negative),
            ETSTikFinityChatConversionStatus::RejectedInvalidNumericField,
            "Negative FollowRole"
        );

        FTSTikFinityDecodedChatMessage AboveMaximum =
            MakeMinimalValidMessage();
        AboveMaximum.Data->User->TeamMemberLevel = Int32Max + 1;
        RequireConversionStatus(
            FTSTikFinityChatConverter::Convert(AboveMaximum),
            ETSTikFinityChatConversionStatus::RejectedInvalidNumericField,
            "Above-max TeamMemberLevel"
        );
    }

    void TestTextAndDuplicateEmotesArePreserved()
    {
        FTSTikFinityDecodedChatMessage Message = MakeMinimalValidMessage();
        Message.Data->Comment = "  preserve surrounding whitespace\t";
        const FTSTikFinityDecodedEmote Duplicate =
            MakeValidEmote("duplicate", "https://example.test/duplicate.png");
        Message.Data->Emotes = {Duplicate, Duplicate};

        const FTSTikFinityChatConversionResult Result =
            FTSTikFinityChatConverter::Convert(Message);
        RequireConversionStatus(
            Result,
            ETSTikFinityChatConversionStatus::Converted,
            "Text and duplicate preservation"
        );
        Require(
            Result.Input->Comment == "  preserve surrounding whitespace\t",
            "Comment must not be trimmed"
        );
        Require(Result.Input->Emotes.size() == 2, "Duplicate emote count");
        Require(
            Result.Input->Emotes[0].EmoteId == "duplicate" &&
                Result.Input->Emotes[1].EmoteId == "duplicate" &&
                Result.Input->Emotes[0].EmoteImageUrl ==
                    Result.Input->Emotes[1].EmoteImageUrl,
            "Duplicate emotes must remain identical and ordered"
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
        {"Complete Chat conversion", &TestCompleteConversion},
        {"Optional field defaults", &TestOptionalFieldDefaults},
        {"Non-Chat event is ignored", &TestNonChatEventIsIgnoredWithoutChatInspection},
        {"Invalid envelope and missing data or user", &TestInvalidEnvelopeAndMissingDataOrUser},
        {"Missing or empty user identity", &TestMissingOrEmptyUserIdentity},
        {"Emote-only Chat is converted", &TestEmoteOnlyChatIsConverted},
        {"Empty content is rejected", &TestEmptyContentIsRejected},
        {"Invalid emotes reject whole message", &TestInvalidEmoteVariantsRejectWholeMessage},
        {"Numeric representation boundaries", &TestNumericRepresentationBoundaries},
        {"Text and duplicate emotes are preserved", &TestTextAndDuplicateEmotesArePreserved},
        {"Complete Follow conversion", &TestCompleteFollowConversion},
        {"Follow optional field defaults", &TestFollowOptionalFieldDefaults},
        {"Non-Follow event is ignored", &TestNonFollowEventIsIgnoredWithoutFollowInspection},
        {"Invalid envelope and missing data or user", &TestFollowInvalidEnvelopeAndMissingDataOrUser},
        {"Missing or empty Follow identity", &TestFollowMissingOrEmptyUserIdentity},
        {"Follow numeric representation boundaries", &TestFollowNumericRepresentationBoundaries},
        {"JSON Follow decode and converter integration", &TestJsonDecodeToFollowInput}
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
            std::cerr
                << "FAIL: " << Test.Name
                << " - " << Error.what()
                << '\n';
        }
        catch (...)
        {
            ++FailedCount;
            std::cerr
                << "FAIL: " << Test.Name
                << " - unknown exception\n";
        }
    }

    std::cout
        << "RESULT: " << PassedCount << " passed, "
        << FailedCount << " failed\n";

    return FailedCount == 0 ? 0 : 1;
}
