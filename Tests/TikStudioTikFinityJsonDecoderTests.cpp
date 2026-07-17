#include "TikFinity/TSTikFinityChatConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityMappedEventFormatter.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

    void RequireStatus(
        const FTSTikFinityJsonDecodeResult& Result,
        ETSTikFinityJsonDecodeStatus Status,
        std::string_view ErrorPath,
        const std::string& Context
    )
    {
        Require(Result.Status == Status, Context + ": status");
        Require(Result.ErrorPath == ErrorPath, Context + ": ErrorPath");
        Require(
            Result.Event.has_value() ==
                (Status == ETSTikFinityJsonDecodeStatus::Decoded),
            Context + ": Event invariant"
        );
    }

    template <typename TMessage>
    const TMessage& RequireDecoded(
        const FTSTikFinityJsonDecodeResult& Result,
        std::string_view EventName,
        const std::string& Context
    )
    {
        RequireStatus(
            Result,
            ETSTikFinityJsonDecodeStatus::Decoded,
            "",
            Context
        );
        Require(Result.EventName == EventName, Context + ": EventName");
        Require(
            std::holds_alternative<TMessage>(*Result.Event),
            Context + ": variant"
        );
        return std::get<TMessage>(*Result.Event);
    }

    void RequireCompleteUser(
        const std::optional<FTSTikFinityDecodedUser>& OptionalUser,
        const std::string& Context
    )
    {
        Require(OptionalUser.has_value(), Context + ": User presence");
        const FTSTikFinityDecodedUser& User = *OptionalUser;
        Require(User.UniqueId == "u", Context + ": UniqueId");
        Require(User.Nickname == "n", Context + ": Nickname");
        Require(User.ProfilePictureUrl == "p", Context + ": ProfilePictureUrl");
        Require(User.FollowRole == 1, Context + ": FollowRole");
        Require(User.bIsModerator == true, Context + ": IsModerator");
        Require(User.bIsSubscriber == false, Context + ": IsSubscriber");
        Require(User.bIsNewGifter == true, Context + ": IsNewGifter");
        Require(User.TopGifterRank == 2, Context + ": TopGifterRank");
        Require(User.GifterLevel == 3, Context + ": GifterLevel");
        Require(User.TeamMemberLevel == 4, Context + ": TeamMemberLevel");
    }

    constexpr std::string_view CompleteChatJson = R"json(
{
  "event": "chat",
  "data": {
    "comment": "hello",
    "emotes": [
      {"emoteId": "a", "emoteImageUrl": "url-a"},
      {"emoteId": "b", "emoteImageUrl": "url-b"}
    ],
    "uniqueId": "u",
    "nickname": "n",
    "profilePictureUrl": "p",
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

    constexpr std::string_view CompleteGiftJson = R"json(
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
    "uniqueId": "u",
    "nickname": "n",
    "profilePictureUrl": "p",
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

    constexpr std::string_view CompleteLikeJson = R"json(
{"event":"like","data":{"likeCount":5,"totalLikeCount":50,
"uniqueId":"u","nickname":"n","profilePictureUrl":"p","followRole":1,
"isModerator":true,"isSubscriber":false,"isNewGifter":true,
"topGifterRank":2,"gifterLevel":3,"teamMemberLevel":4}}
)json";

    constexpr std::string_view CompleteFollowJson = R"json(
{"event":"follow","data":{"uniqueId":"u","nickname":"n",
"profilePictureUrl":"p","followRole":1,"isModerator":true,
"isSubscriber":false,"isNewGifter":true,"topGifterRank":2,
"gifterLevel":3,"teamMemberLevel":4}}
)json";

    constexpr std::string_view CompleteShareJson = R"json(
{"event":"share","data":{"uniqueId":"u","nickname":"n",
"profilePictureUrl":"p","followRole":1,"isModerator":true,
"isSubscriber":false,"isNewGifter":true,"topGifterRank":2,
"gifterLevel":3,"teamMemberLevel":4}}
)json";

    constexpr std::string_view CompleteMemberJson = R"json(
{"event":"member","data":{"actionId":9,"uniqueId":"u","nickname":"n",
"profilePictureUrl":"p","followRole":1,"isModerator":true,
"isSubscriber":false,"isNewGifter":true,"topGifterRank":2,
"gifterLevel":3,"teamMemberLevel":4}}
)json";

    constexpr std::string_view CompleteRoomUserJson = R"json(
{
  "event": "roomUser",
  "data": {
    "viewerCount": 100,
    "topGifterRank": 7,
    "topViewers": [
      {"coinCount": 11, "user": {"uniqueId": "a", "nickname": "A",
        "profilePictureUrl": "pa", "isModerator": true,
        "isSubscriber": false, "gifterLevel": 1, "teamMemberLevel": 2}},
      {"coinCount": 22, "user": {"uniqueId": "b", "nickname": "B",
        "profilePictureUrl": "pb", "isModerator": false,
        "isSubscriber": true, "gifterLevel": 3, "teamMemberLevel": 4}}
    ]
  }
}
)json";

    void TestCompleteChatAndConverterIntegration()
    {
        const FTSTikFinityJsonDecodeResult Result =
            FTSTikFinityJsonEventDecoder::Decode(CompleteChatJson);
        const FTSTikFinityDecodedChatMessage& Chat =
            RequireDecoded<FTSTikFinityDecodedChatMessage>(
                Result, "chat", "Complete Chat"
            );
        Require(Chat.Data.has_value(), "Complete Chat: Data");
        Require(Chat.Data->Comment == "hello", "Complete Chat: Comment");
        Require(Chat.Data->Emotes.size() == 2, "Complete Chat: emote count");
        Require(Chat.Data->Emotes[0].EmoteId == "a", "Complete Chat: emote a");
        Require(
            Chat.Data->Emotes[1].EmoteImageUrl == "url-b",
            "Complete Chat: emote b URL"
        );
        RequireCompleteUser(Chat.Data->User, "Complete Chat");

        const std::string Formatted =
            FTSTikFinityMappedEventFormatter::Format(*Result.Event);
        Require(
            Formatted.find("Event: chat\nComment: hello\nEmoteCount: 2\n") == 0,
            "Complete Chat: formatter"
        );

        const FTSTikFinityChatConversionResult Converted =
            FTSTikFinityChatConverter::Convert(Chat);
        Require(
            Converted.Status == ETSTikFinityChatConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Complete Chat: converter"
        );
        const FTSChatInput& Input = *Converted.Input;
        Require(Input.Comment == "hello", "Complete Chat input: Comment");
        Require(Input.Emotes.size() == 2, "Complete Chat input: emote count");
        Require(Input.Emotes[0].EmoteId == "a", "Complete Chat input: emote a");
        Require(Input.Emotes[1].EmoteImageUrl == "url-b", "Complete Chat input: URL");
        Require(Input.User.UniqueId == "u", "Complete Chat input: UniqueId");
        Require(Input.User.Nickname == "n", "Complete Chat input: Nickname");
        Require(Input.User.ProfilePictureUrl == "p", "Complete Chat input: Picture");
        Require(Input.User.FollowRole == 1, "Complete Chat input: FollowRole");
        Require(Input.User.bIsModerator, "Complete Chat input: Moderator");
        Require(!Input.User.bIsSubscriber, "Complete Chat input: Subscriber");
        Require(Input.User.bIsNewGifter, "Complete Chat input: NewGifter");
        Require(Input.User.TopGifterRank == 2, "Complete Chat input: Rank");
        Require(Input.User.GifterLevel == 3, "Complete Chat input: GifterLevel");
        Require(Input.User.TeamMemberLevel == 4, "Complete Chat input: TeamLevel");
    }

    void TestCompleteGift()
    {
        const FTSTikFinityJsonDecodeResult Result =
            FTSTikFinityJsonEventDecoder::Decode(CompleteGiftJson);
        const auto& Message = RequireDecoded<FTSTikFinityDecodedGiftMessage>(
            Result, "gift", "Complete Gift"
        );
        const auto& Data = *Message.Data;
        Require(Data.GiftId == 10, "GiftId");
        Require(Data.GiftName == "Rose", "GiftName");
        Require(Data.GiftPictureUrl == "gift-url", "GiftPictureUrl");
        Require(Data.DiamondCount == 20, "DiamondCount");
        Require(Data.RepeatCount == 30, "RepeatCount");
        Require(Data.GiftType == 40, "GiftType");
        Require(Data.Describe == "desc", "Describe");
        Require(Data.bRepeatEnd == true, "RepeatEnd");
        Require(Data.GroupId == "group", "GroupId");
        RequireCompleteUser(Data.User, "Complete Gift");
        Require(
            FTSTikFinityMappedEventFormatter::Format(*Result.Event).find(
                "GiftId: 10\nGiftName: Rose\n"
            ) != std::string::npos,
            "Complete Gift formatter"
        );
    }

    void TestCompleteLike()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(CompleteLikeJson);
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedLikeMessage>(
            Result, "like", "Complete Like"
        ).Data;
        Require(Data.LikeCount == 5, "LikeCount");
        Require(Data.TotalLikeCount == 50, "TotalLikeCount");
        RequireCompleteUser(Data.User, "Complete Like");
    }

    void TestCompleteFollow()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(CompleteFollowJson);
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedFollowMessage>(
            Result, "follow", "Complete Follow"
        ).Data;
        RequireCompleteUser(Data.User, "Complete Follow");
    }

    void TestCompleteShare()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(CompleteShareJson);
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedShareMessage>(
            Result, "share", "Complete Share"
        ).Data;
        RequireCompleteUser(Data.User, "Complete Share");
    }

    void TestCompleteMember()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(CompleteMemberJson);
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedMemberMessage>(
            Result, "member", "Complete Member"
        ).Data;
        Require(Data.ActionId == 9, "ActionId");
        RequireCompleteUser(Data.User, "Complete Member");
    }

    void TestCompleteRoomUser()
    {
        const auto Result =
            FTSTikFinityJsonEventDecoder::Decode(CompleteRoomUserJson);
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedRoomUserMessage>(
            Result, "roomUser", "Complete RoomUser"
        ).Data;
        Require(Data.ViewerCount == 100, "RoomUser ViewerCount");
        Require(Data.TopGifterRank == 7, "RoomUser TopGifterRank");
        Require(Data.TopViewers.size() == 2, "RoomUser viewer count");
        Require(Data.TopViewers[0].CoinCount == 11, "RoomUser coin A");
        Require(Data.TopViewers[0].User->UniqueId == "a", "RoomUser user A");
        Require(Data.TopViewers[0].User->Nickname == "A", "RoomUser nickname A");
        Require(Data.TopViewers[0].User->ProfilePictureUrl == "pa", "RoomUser picture A");
        Require(Data.TopViewers[0].User->bIsModerator == true, "RoomUser moderator A");
        Require(Data.TopViewers[0].User->bIsSubscriber == false, "RoomUser subscriber A");
        Require(Data.TopViewers[0].User->GifterLevel == 1, "RoomUser gifter A");
        Require(Data.TopViewers[0].User->TeamMemberLevel == 2, "RoomUser team A");
        Require(Data.TopViewers[1].CoinCount == 22, "RoomUser coin B");
        Require(Data.TopViewers[1].User->UniqueId == "b", "RoomUser user B");
        Require(Data.TopViewers[1].User->Nickname == "B", "RoomUser nickname B");
        Require(Data.TopViewers[1].User->ProfilePictureUrl == "pb", "RoomUser picture B");
        Require(Data.TopViewers[1].User->bIsModerator == false, "RoomUser moderator B");
        Require(Data.TopViewers[1].User->bIsSubscriber == true, "RoomUser subscriber B");
        Require(Data.TopViewers[1].User->GifterLevel == 3, "RoomUser gifter B");
        Require(Data.TopViewers[1].User->TeamMemberLevel == 4, "RoomUser team B");
        Require(
            FTSTikFinityMappedEventFormatter::Format(*Result.Event).find(
                "TopViewers[1].User.TeamMemberLevel: 4\n"
            ) != std::string::npos,
            "RoomUser formatter"
        );
    }

    void TestFlatUserLocation()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"follow","data":{"uniqueId":"flat","user":{"uniqueId":"nested"}}})json"
        );
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedFollowMessage>(
            Result, "follow", "Flat user"
        ).Data;
        Require(Data.User->UniqueId == "flat", "Flat user must come from data");
    }

    void TestEmptyDataKeepsPresentUser()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"follow","data":{}})json"
        );
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedFollowMessage>(
            Result, "follow", "Empty Follow"
        ).Data;
        Require(Data.User.has_value(), "Empty Follow User presence");
        Require(!Data.User->UniqueId.has_value(), "Empty Follow UniqueId");
        Require(!Data.User->TeamMemberLevel.has_value(), "Empty Follow TeamLevel");
        Require(
            FTSTikFinityMappedEventFormatter::Format(*Result.Event).find(
                "User.UniqueId: <missing>\n"
            ) != std::string::npos,
            "Empty Follow formatter missing"
        );
    }

    void TestUnknownEventDoesNotRequireData()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"subscribe"})json"
        );
        RequireStatus(
            Result,
            ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent,
            "",
            "Unknown event"
        );
        Require(Result.EventName == "subscribe", "Unknown EventName");
    }

    void TestExactEventNames()
    {
        for (const std::string_view Name : {"Chat", "Gift", "roomuser", "ROOMUSER"})
        {
            const std::string Json =
                "{\"event\":\"" + std::string(Name) + "\"}";
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Json);
            RequireStatus(
                Result,
                ETSTikFinityJsonDecodeStatus::IgnoredUnknownEvent,
                "",
                "Exact name " + std::string(Name)
            );
        }
    }

    void TestInvalidEnvelopeVariants()
    {
        struct FCase
        {
            std::string_view Json;
            ETSTikFinityJsonDecodeStatus Status;
            std::string_view Path;
        };
        const std::vector<FCase> Cases{
            {"", ETSTikFinityJsonDecodeStatus::RejectedEmptyFrame, ""},
            {" \t\r\n", ETSTikFinityJsonDecodeStatus::RejectedEmptyFrame, ""},
            {"{", ETSTikFinityJsonDecodeStatus::RejectedMalformedJson, ""},
            {"[]", ETSTikFinityJsonDecodeStatus::RejectedRootNotObject, "$"},
            {"\"root\"", ETSTikFinityJsonDecodeStatus::RejectedRootNotObject, "$"},
            {"null", ETSTikFinityJsonDecodeStatus::RejectedRootNotObject, "$"},
            {"{}", ETSTikFinityJsonDecodeStatus::RejectedMissingEvent, "event"},
            {R"json({"event":1})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName, "event"},
            {R"json({"event":true})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName, "event"},
            {R"json({"event":""})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidEventName, "event"},
            {R"json({"event":"chat"})json", ETSTikFinityJsonDecodeStatus::RejectedMissingData, "data"},
            {R"json({"event":"chat","data":[]})json", ETSTikFinityJsonDecodeStatus::RejectedDataNotObject, "data"},
            {R"json({"event":"chat","data":"x"})json", ETSTikFinityJsonDecodeStatus::RejectedDataNotObject, "data"}
        };
        for (std::size_t Index = 0; Index < Cases.size(); ++Index)
        {
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Cases[Index].Json);
            RequireStatus(
                Result,
                Cases[Index].Status,
                Cases[Index].Path,
                "Envelope case " + std::to_string(Index)
            );
        }
    }

    void TestInvalidScalarTypes()
    {
        struct FCase
        {
            std::string_view Json;
            std::string_view Path;
        };
        const std::vector<FCase> Cases{
            {R"json({"event":"gift","data":{"giftId":"1"}})json", "data.giftId"},
            {R"json({"event":"gift","data":{"repeatEnd":1}})json", "data.repeatEnd"},
            {R"json({"event":"follow","data":{"isModerator":1}})json", "data.isModerator"},
            {R"json({"event":"follow","data":{"nickname":false}})json", "data.nickname"}
        };
        for (std::size_t Index = 0; Index < Cases.size(); ++Index)
        {
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Cases[Index].Json);
            RequireStatus(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                Cases[Index].Path,
                "Scalar case " + std::to_string(Index)
            );
            Require(!Result.EventName.empty(), "Scalar EventName retained");
        }
    }

    void TestInvalidArraysAndElements()
    {
        struct FCase
        {
            std::string_view Json;
            ETSTikFinityJsonDecodeStatus Status;
            std::string_view Path;
        };
        const std::vector<FCase> Cases{
            {R"json({"event":"chat","data":{"emotes":{}}})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType, "data.emotes"},
            {R"json({"event":"roomUser","data":{"topViewers":"invalid"}})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType, "data.topViewers"},
            {R"json({"event":"chat","data":{"emotes":["invalid"]}})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidArrayElement, "data.emotes[0]"},
            {R"json({"event":"roomUser","data":{"topViewers":[false]}})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidArrayElement, "data.topViewers[0]"},
            {R"json({"event":"roomUser","data":{"topViewers":[{"user":[]}]}})json", ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType, "data.topViewers[0].user"}
        };
        for (std::size_t Index = 0; Index < Cases.size(); ++Index)
        {
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Cases[Index].Json);
            RequireStatus(
                Result,
                Cases[Index].Status,
                Cases[Index].Path,
                "Array case " + std::to_string(Index)
            );
        }
    }

    void TestInvalidNestedFields()
    {
        const auto Emote = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"chat","data":{"emotes":[{"emoteImageUrl":123}]}})json"
        );
        RequireStatus(
            Emote,
            ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
            "data.emotes[0].emoteImageUrl",
            "Nested emote"
        );

        const auto Viewer = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"roomUser","data":{"topViewers":[{"user":{"gifterLevel":true}}]}})json"
        );
        RequireStatus(
            Viewer,
            ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
            "data.topViewers[0].user.gifterLevel",
            "Nested viewer"
        );
    }

    void TestNumericBoundaries()
    {
        const std::vector<std::int64_t> Accepted{
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max(),
            0
        };
        for (const std::int64_t Value : Accepted)
        {
            const std::string Json =
                "{\"event\":\"gift\",\"data\":{\"giftId\":" +
                std::to_string(Value) + "}}";
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Json);
            const auto& Data = *RequireDecoded<FTSTikFinityDecodedGiftMessage>(
                Result, "gift", "Accepted numeric"
            ).Data;
            Require(Data.GiftId == Value, "Accepted numeric value");
        }

        const auto UnsignedMax = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"gift","data":{"giftId":9223372036854775807}})json"
        );
        RequireStatus(
            UnsignedMax,
            ETSTikFinityJsonDecodeStatus::Decoded,
            "",
            "Unsigned INT64_MAX"
        );

        const auto OutOfRange = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"gift","data":{"giftId":9223372036854775808}})json"
        );
        RequireStatus(
            OutOfRange,
            ETSTikFinityJsonDecodeStatus::RejectedNumericOutOfRange,
            "data.giftId",
            "Unsigned out of range"
        );

        for (const std::string_view Value : {"1.5", "\"1\"", "true"})
        {
            const std::string Json =
                "{\"event\":\"gift\",\"data\":{\"giftId\":" +
                std::string(Value) + "}}";
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Json);
            RequireStatus(
                Result,
                ETSTikFinityJsonDecodeStatus::RejectedInvalidFieldType,
                "data.giftId",
                "Invalid numeric type"
            );
        }
    }

    void TestAbsentOptionalFields()
    {
        const auto Result = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"gift","data":{}})json"
        );
        const auto& Data = *RequireDecoded<FTSTikFinityDecodedGiftMessage>(
            Result, "gift", "Absent optional fields"
        ).Data;
        Require(!Data.GiftId.has_value(), "Absent GiftId");
        Require(!Data.GiftName.has_value(), "Absent GiftName");
        Require(Data.User.has_value(), "Absent fields User presence");
        Require(!Data.User->FollowRole.has_value(), "Absent FollowRole");
        Require(
            FTSTikFinityMappedEventFormatter::Format(*Result.Event).find(
                "GiftId: <missing>\n"
            ) != std::string::npos,
            "Absent formatter"
        );
    }

    void TestArrayOrderAndDuplicates()
    {
        const auto ChatResult = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"chat","data":{"emotes":[{"emoteId":"a"},{"emoteId":"a"},{"emoteId":"b"}]}})json"
        );
        const auto& Emotes = RequireDecoded<FTSTikFinityDecodedChatMessage>(
            ChatResult, "chat", "Duplicate emotes"
        ).Data->Emotes;
        Require(Emotes.size() == 3, "Duplicate emote count");
        Require(
            Emotes[0].EmoteId == "a" && Emotes[1].EmoteId == "a" &&
                Emotes[2].EmoteId == "b",
            "Duplicate emote order"
        );

        const auto RoomResult = FTSTikFinityJsonEventDecoder::Decode(
            R"json({"event":"roomUser","data":{"topViewers":[{"coinCount":1},{"coinCount":1},{"coinCount":2}]}})json"
        );
        const auto& Viewers = RequireDecoded<FTSTikFinityDecodedRoomUserMessage>(
            RoomResult, "roomUser", "Duplicate viewers"
        ).Data->TopViewers;
        Require(Viewers.size() == 3, "Duplicate viewer count");
        Require(
            Viewers[0].CoinCount == 1 && Viewers[1].CoinCount == 1 &&
                Viewers[2].CoinCount == 2,
            "Duplicate viewer order"
        );
    }

    void TestKindNameVariantConsistency()
    {
        const std::vector<std::string_view> Fixtures{
            R"json({"event":"chat","data":{}})json",
            R"json({"event":"gift","data":{}})json",
            R"json({"event":"like","data":{}})json",
            R"json({"event":"follow","data":{}})json",
            R"json({"event":"share","data":{}})json",
            R"json({"event":"roomUser","data":{}})json",
            R"json({"event":"member","data":{}})json"
        };
        for (const std::string_view Fixture : Fixtures)
        {
            const auto Result = FTSTikFinityJsonEventDecoder::Decode(Fixture);
            RequireStatus(
                Result,
                ETSTikFinityJsonDecodeStatus::Decoded,
                "",
                "Kind consistency"
            );
            const auto Parsed = TryParseTikFinityMappedEventKind(Result.EventName);
            Require(Parsed.has_value(), "Kind consistency parsed");
            Require(
                *Parsed == GetTikFinityMappedEventKind(*Result.Event),
                "Kind consistency variant"
            );
            Require(
                GetTikFinityMappedEventName(*Parsed) == Result.EventName,
                "Kind consistency name"
            );
        }
    }

    void TestFormatterDeterminism()
    {
        const auto Result =
            FTSTikFinityJsonEventDecoder::Decode(CompleteRoomUserJson);
        const std::string First =
            FTSTikFinityMappedEventFormatter::Format(*Result.Event);
        const std::string Second =
            FTSTikFinityMappedEventFormatter::Format(*Result.Event);
        Require(First == Second, "Formatter must be deterministic");
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
        {"Complete Chat decode and converter integration", &TestCompleteChatAndConverterIntegration},
        {"Complete Gift decode", &TestCompleteGift},
        {"Complete Like decode", &TestCompleteLike},
        {"Complete Follow decode", &TestCompleteFollow},
        {"Complete Share decode", &TestCompleteShare},
        {"Complete Member decode", &TestCompleteMember},
        {"Complete RoomUser decode", &TestCompleteRoomUser},
        {"Flat user location", &TestFlatUserLocation},
        {"Empty data keeps present user", &TestEmptyDataKeepsPresentUser},
        {"Unknown event does not require data", &TestUnknownEventDoesNotRequireData},
        {"Exact event names", &TestExactEventNames},
        {"Invalid envelope variants", &TestInvalidEnvelopeVariants},
        {"Invalid scalar types", &TestInvalidScalarTypes},
        {"Invalid arrays and elements", &TestInvalidArraysAndElements},
        {"Invalid nested fields", &TestInvalidNestedFields},
        {"Numeric boundaries", &TestNumericBoundaries},
        {"Absent optional fields", &TestAbsentOptionalFields},
        {"Array order and duplicates", &TestArrayOrderAndDuplicates},
        {"Kind name and variant consistency", &TestKindNameVariantConsistency},
        {"Formatter determinism", &TestFormatterDeterminism}
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
