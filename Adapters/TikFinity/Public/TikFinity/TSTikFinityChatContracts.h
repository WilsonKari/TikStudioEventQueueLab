#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct FTSTikFinityDecodedEmote
{
    // Ausencia y cadena vacía permanecen distintas hasta la conversión.
    std::optional<std::string> EmoteId;
    std::optional<std::string> EmoteImageUrl;
};

struct FTSTikFinityDecodedUser
{
    std::optional<std::string> UniqueId;
    std::optional<std::string> Nickname;
    std::optional<std::string> ProfilePictureUrl;

    // La frontera conserva int64 para validar explícitamente el destino int32.
    std::optional<std::int64_t> FollowRole;

    std::optional<bool> bIsModerator;
    std::optional<bool> bIsSubscriber;
    std::optional<bool> bIsNewGifter;

    std::optional<std::int64_t> TopGifterRank;
    std::optional<std::int64_t> GifterLevel;
    std::optional<std::int64_t> TeamMemberLevel;
};

struct FTSTikFinityDecodedChatData
{
    std::optional<std::string> Comment;
    std::vector<FTSTikFinityDecodedEmote> Emotes;
    std::optional<FTSTikFinityDecodedUser> User;
};

// Contrato decodificado específico de Chat; no representa un evento universal.
struct FTSTikFinityDecodedChatMessage
{
    std::string EventName;
    std::optional<FTSTikFinityDecodedChatData> Data;
};

enum class ETSTikFinityChatConversionStatus : std::uint8_t
{
    Converted,
    IgnoredNonChatEvent,
    RejectedInvalidEnvelope,
    RejectedMissingData,
    RejectedMissingUser,
    RejectedMissingUserIdentity,
    RejectedEmptyContent,
    RejectedInvalidNumericField,
    RejectedInvalidEmote
};

struct FTSTikFinityChatConversionResult
{
    ETSTikFinityChatConversionStatus Status =
        ETSTikFinityChatConversionStatus::RejectedInvalidEnvelope;

    // Sólo Converted puede contener una entrada normalizada.
    std::optional<FTSChatInput> Input;
};
