#include "TikFinity/TSTikFinityRoomUserConverter.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityRoomUserConversionResult MakeRejectedResult(
        ETSTikFinityRoomUserConversionStatus Status
    )
    {
        FTSTikFinityRoomUserConversionResult Result;
        Result.Status = Status;
        return Result;
    }

    [[nodiscard]]
    bool TryConvertNonNegative(
        std::int64_t Source,
        std::int32_t& Destination
    ) noexcept
    {
        if (Source < 0 ||
            Source > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }

        Destination = static_cast<std::int32_t>(Source);
        return true;
    }

    [[nodiscard]]
    bool TryConvertOptionalNonNegative(
        const std::optional<std::int64_t>& Source,
        std::int32_t& Destination
    ) noexcept
    {
        if (!Source.has_value())
        {
            Destination = 0;
            return true;
        }

        return TryConvertNonNegative(*Source, Destination);
    }
}

FTSTikFinityRoomUserConversionResult
FTSTikFinityRoomUserConverter::Convert(
    const FTSTikFinityDecodedRoomUserMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "roomUser")
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::IgnoredNonRoomUserEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedRoomUserData& Data = *Message.Data;
    if (!Data.ViewerCount.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::RejectedMissingViewerCount
        );
    }

    FTSRoomUserInput Input;
    if (!TryConvertNonNegative(*Data.ViewerCount, Input.ViewerCount))
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::RejectedInvalidNumericField
        );
    }

    if (!TryConvertOptionalNonNegative(
            Data.TopGifterRank,
            Input.TopGifterRank
        ))
    {
        return MakeRejectedResult(
            ETSTikFinityRoomUserConversionStatus::RejectedInvalidNumericField
        );
    }

    Input.TopViewers.reserve(Data.TopViewers.size());
    for (const FTSTikFinityDecodedTopViewer& SourceViewer : Data.TopViewers)
    {
        if (!SourceViewer.CoinCount.has_value())
        {
            return MakeRejectedResult(
                ETSTikFinityRoomUserConversionStatus::
                    RejectedMissingTopViewerCoinCount
            );
        }

        if (!SourceViewer.User.has_value())
        {
            return MakeRejectedResult(
                ETSTikFinityRoomUserConversionStatus::
                    RejectedMissingTopViewerUser
            );
        }

        const FTSTikFinityDecodedRoomUserViewerUser& SourceUser =
            *SourceViewer.User;
        if (!SourceUser.UniqueId.has_value() ||
            SourceUser.UniqueId->empty())
        {
            return MakeRejectedResult(
                ETSTikFinityRoomUserConversionStatus::
                    RejectedMissingTopViewerIdentity
            );
        }

        FTSRoomUserTopViewer Viewer;
        if (!TryConvertNonNegative(*SourceViewer.CoinCount, Viewer.CoinCount) ||
            !TryConvertOptionalNonNegative(
                SourceUser.GifterLevel,
                Viewer.GifterLevel
            ) ||
            !TryConvertOptionalNonNegative(
                SourceUser.TeamMemberLevel,
                Viewer.TeamMemberLevel
            ))
        {
            return MakeRejectedResult(
                ETSTikFinityRoomUserConversionStatus::
                    RejectedInvalidNumericField
            );
        }

        Viewer.UniqueId = *SourceUser.UniqueId;
        Viewer.Nickname = SourceUser.Nickname.value_or(std::string{});
        Viewer.ProfilePictureUrl =
            SourceUser.ProfilePictureUrl.value_or(std::string{});
        Viewer.bIsModerator = SourceUser.bIsModerator.value_or(false);
        Viewer.bIsSubscriber = SourceUser.bIsSubscriber.value_or(false);
        Input.TopViewers.push_back(std::move(Viewer));
    }

    FTSTikFinityRoomUserConversionResult Result;
    Result.Status = ETSTikFinityRoomUserConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
