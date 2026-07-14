#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Instantánea portable de los datos comunes del usuario recibidos con un evento.
struct FTSUserSnapshot
{
    std::string UniqueId;
    std::string Nickname;
    std::string ProfilePictureUrl;
    std::int32_t FollowRole = 0;
    bool bIsModerator = false;
    bool bIsSubscriber = false;
    bool bIsNewGifter = false;
    std::int32_t TopGifterRank = 0;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;
};

// Datos portables de un emote recibido con un evento Chat.
struct FTSEmoteInfo
{
    std::string EmoteId;
    std::string EmoteImageUrl;
};

struct FTSChatInput
{
    std::string Comment;
    std::vector<FTSEmoteInfo> Emotes;
    FTSUserSnapshot User;
};

struct FTSGiftInput
{
    std::int32_t GiftId = 0;
    std::string GiftName;
    std::string GiftPictureUrl;
    std::int32_t DiamondCount = 0;
    std::int32_t RepeatCount = 1;
    std::int32_t GiftType = 0;
    std::string Describe;
    bool bRepeatEnd = false;
    std::string GroupId;
    FTSUserSnapshot User;
};

struct FTSLikeInput
{
    std::int32_t LikeCount = 0;
    std::int32_t TotalLikeCount = 0;
    FTSUserSnapshot User;
};

struct FTSMemberInput
{
    std::int32_t ActionId = 0;
    FTSUserSnapshot User;
};

// Los top viewers tienen un contrato distinto al usuario común.
struct FTSRoomUserTopViewer
{
    std::string UniqueId;
    std::string Nickname;
    std::string ProfilePictureUrl;
    std::int32_t CoinCount = 0;
    bool bIsModerator = false;
    bool bIsSubscriber = false;
    std::int32_t GifterLevel = 0;
    std::int32_t TeamMemberLevel = 0;
};

struct FTSRoomUserInput
{
    std::int32_t ViewerCount = 0;
    std::int32_t TopGifterRank = 0;
    std::vector<FTSRoomUserTopViewer> TopViewers;
};

struct FTSShareInput
{
    FTSUserSnapshot User;
};

struct FTSFollowInput
{
    FTSUserSnapshot User;
};
