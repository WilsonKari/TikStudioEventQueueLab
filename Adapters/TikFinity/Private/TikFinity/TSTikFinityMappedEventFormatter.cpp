#include "TikFinity/TSTikFinityMappedEventFormatter.h"

#include <cstddef>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace
{
    template <typename TValue>
    void AppendOptionalField(
        std::ostringstream& Output,
        std::string_view Label,
        const std::optional<TValue>* Value
    )
    {
        Output << Label << ": ";
        if (Value == nullptr || !Value->has_value())
        {
            Output << "<missing>\n";
            return;
        }

        if constexpr (std::is_same_v<TValue, bool>)
        {
            Output << (**Value ? "true" : "false") << '\n';
        }
        else
        {
            Output << **Value << '\n';
        }
    }

    void AppendCommonUser(
        std::ostringstream& Output,
        const FTSTikFinityDecodedUser* User
    )
    {
        AppendOptionalField(
            Output, "User.UniqueId", User == nullptr ? nullptr : &User->UniqueId
        );
        AppendOptionalField(
            Output, "User.Nickname", User == nullptr ? nullptr : &User->Nickname
        );
        AppendOptionalField(
            Output,
            "User.ProfilePictureUrl",
            User == nullptr ? nullptr : &User->ProfilePictureUrl
        );
        AppendOptionalField(
            Output,
            "User.FollowRole",
            User == nullptr ? nullptr : &User->FollowRole
        );
        AppendOptionalField(
            Output,
            "User.IsModerator",
            User == nullptr ? nullptr : &User->bIsModerator
        );
        AppendOptionalField(
            Output,
            "User.IsSubscriber",
            User == nullptr ? nullptr : &User->bIsSubscriber
        );
        AppendOptionalField(
            Output,
            "User.IsNewGifter",
            User == nullptr ? nullptr : &User->bIsNewGifter
        );
        AppendOptionalField(
            Output,
            "User.TopGifterRank",
            User == nullptr ? nullptr : &User->TopGifterRank
        );
        AppendOptionalField(
            Output,
            "User.GifterLevel",
            User == nullptr ? nullptr : &User->GifterLevel
        );
        AppendOptionalField(
            Output,
            "User.TeamMemberLevel",
            User == nullptr ? nullptr : &User->TeamMemberLevel
        );
    }

    void AppendRoomUserViewerUser(
        std::ostringstream& Output,
        std::size_t Index,
        const FTSTikFinityDecodedRoomUserViewerUser* User
    )
    {
        const std::string Prefix =
            "TopViewers[" + std::to_string(Index) + "].User.";
        AppendOptionalField(
            Output,
            Prefix + "UniqueId",
            User == nullptr ? nullptr : &User->UniqueId
        );
        AppendOptionalField(
            Output,
            Prefix + "Nickname",
            User == nullptr ? nullptr : &User->Nickname
        );
        AppendOptionalField(
            Output,
            Prefix + "ProfilePictureUrl",
            User == nullptr ? nullptr : &User->ProfilePictureUrl
        );
        AppendOptionalField(
            Output,
            Prefix + "IsModerator",
            User == nullptr ? nullptr : &User->bIsModerator
        );
        AppendOptionalField(
            Output,
            Prefix + "IsSubscriber",
            User == nullptr ? nullptr : &User->bIsSubscriber
        );
        AppendOptionalField(
            Output,
            Prefix + "GifterLevel",
            User == nullptr ? nullptr : &User->GifterLevel
        );
        AppendOptionalField(
            Output,
            Prefix + "TeamMemberLevel",
            User == nullptr ? nullptr : &User->TeamMemberLevel
        );
    }
}

std::string FTSTikFinityMappedEventFormatter::Format(
    const FTSTikFinityMappedEvent& Event
)
{
    std::ostringstream Output;
    Output.imbue(std::locale::classic());
    Output << "Event: "
           << GetTikFinityMappedEventName(GetTikFinityMappedEventKind(Event))
           << '\n';

    std::visit(
        [&Output](const auto& Message)
        {
            using FMessage = std::decay_t<decltype(Message)>;

            if constexpr (std::is_same_v<
                              FMessage,
                              FTSTikFinityDecodedChatMessage
                          >)
            {
                const FTSTikFinityDecodedChatData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendOptionalField(
                    Output,
                    "Comment",
                    Data == nullptr ? nullptr : &Data->Comment
                );
                Output << "EmoteCount: "
                       << (Data == nullptr ? 0 : Data->Emotes.size()) << '\n';
                if (Data != nullptr)
                {
                    for (std::size_t Index = 0;
                         Index < Data->Emotes.size();
                         ++Index)
                    {
                        const std::string Prefix =
                            "Emotes[" + std::to_string(Index) + "].";
                        AppendOptionalField(
                            Output,
                            Prefix + "EmoteId",
                            &Data->Emotes[Index].EmoteId
                        );
                        AppendOptionalField(
                            Output,
                            Prefix + "EmoteImageUrl",
                            &Data->Emotes[Index].EmoteImageUrl
                        );
                    }
                }
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedGiftMessage
                               >)
            {
                const FTSTikFinityDecodedGiftData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendOptionalField(
                    Output, "GiftId", Data == nullptr ? nullptr : &Data->GiftId
                );
                AppendOptionalField(
                    Output,
                    "GiftName",
                    Data == nullptr ? nullptr : &Data->GiftName
                );
                AppendOptionalField(
                    Output,
                    "GiftPictureUrl",
                    Data == nullptr ? nullptr : &Data->GiftPictureUrl
                );
                AppendOptionalField(
                    Output,
                    "DiamondCount",
                    Data == nullptr ? nullptr : &Data->DiamondCount
                );
                AppendOptionalField(
                    Output,
                    "RepeatCount",
                    Data == nullptr ? nullptr : &Data->RepeatCount
                );
                AppendOptionalField(
                    Output,
                    "GiftType",
                    Data == nullptr ? nullptr : &Data->GiftType
                );
                AppendOptionalField(
                    Output,
                    "Describe",
                    Data == nullptr ? nullptr : &Data->Describe
                );
                AppendOptionalField(
                    Output,
                    "RepeatEnd",
                    Data == nullptr ? nullptr : &Data->bRepeatEnd
                );
                AppendOptionalField(
                    Output,
                    "GroupId",
                    Data == nullptr ? nullptr : &Data->GroupId
                );
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedLikeMessage
                               >)
            {
                const FTSTikFinityDecodedLikeData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendOptionalField(
                    Output,
                    "LikeCount",
                    Data == nullptr ? nullptr : &Data->LikeCount
                );
                AppendOptionalField(
                    Output,
                    "TotalLikeCount",
                    Data == nullptr ? nullptr : &Data->TotalLikeCount
                );
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedFollowMessage
                               >)
            {
                const FTSTikFinityDecodedFollowData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedShareMessage
                               >)
            {
                const FTSTikFinityDecodedShareData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedMemberMessage
                               >)
            {
                const FTSTikFinityDecodedMemberData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendOptionalField(
                    Output,
                    "ActionId",
                    Data == nullptr ? nullptr : &Data->ActionId
                );
                AppendCommonUser(
                    Output,
                    Data != nullptr && Data->User ? &*Data->User : nullptr
                );
            }
            else if constexpr (std::is_same_v<
                                   FMessage,
                                   FTSTikFinityDecodedRoomUserMessage
                               >)
            {
                const FTSTikFinityDecodedRoomUserData* Data =
                    Message.Data ? &*Message.Data : nullptr;
                AppendOptionalField(
                    Output,
                    "ViewerCount",
                    Data == nullptr ? nullptr : &Data->ViewerCount
                );
                AppendOptionalField(
                    Output,
                    "TopGifterRank",
                    Data == nullptr ? nullptr : &Data->TopGifterRank
                );
                Output << "TopViewerCount: "
                       << (Data == nullptr ? 0 : Data->TopViewers.size())
                       << '\n';
                if (Data != nullptr)
                {
                    for (std::size_t Index = 0;
                         Index < Data->TopViewers.size();
                         ++Index)
                    {
                        const FTSTikFinityDecodedTopViewer& TopViewer =
                            Data->TopViewers[Index];
                        const std::string Prefix =
                            "TopViewers[" + std::to_string(Index) + "].";
                        AppendOptionalField(
                            Output,
                            Prefix + "CoinCount",
                            &TopViewer.CoinCount
                        );
                        AppendRoomUserViewerUser(
                            Output,
                            Index,
                            TopViewer.User ? &*TopViewer.User : nullptr
                        );
                    }
                }
            }
        },
        Event
    );

    return Output.str();
}
