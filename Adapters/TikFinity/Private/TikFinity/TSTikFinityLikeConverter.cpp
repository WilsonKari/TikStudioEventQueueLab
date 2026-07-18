#include "TikFinity/TSTikFinityLikeConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityLikeConversionResult MakeRejectedResult(
        ETSTikFinityLikeConversionStatus Status
    )
    {
        FTSTikFinityLikeConversionResult Result;
        Result.Status = Status;
        return Result;
    }

    [[nodiscard]]
    bool TryConvertRequiredNumericField(
        const std::optional<std::int64_t>& Source,
        std::int32_t& Destination
    ) noexcept
    {
        if (!Source.has_value() ||
            *Source < 0 ||
            *Source > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }

        Destination = static_cast<std::int32_t>(*Source);
        return true;
    }
}

FTSTikFinityLikeConversionResult FTSTikFinityLikeConverter::Convert(
    const FTSTikFinityDecodedLikeMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "like")
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::IgnoredNonLikeEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedLikeData& Data = *Message.Data;
    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;
    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedMissingUserIdentity
        );
    }

    if (!Data.LikeCount.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedMissingLikeCount
        );
    }

    if (!Data.TotalLikeCount.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedMissingTotalLikeCount
        );
    }

    std::int32_t ConvertedLikeCount = 0;
    std::int32_t ConvertedTotalLikeCount = 0;
    if (!TryConvertRequiredNumericField(
            Data.LikeCount,
            ConvertedLikeCount
        ) ||
        !TryConvertRequiredNumericField(
            Data.TotalLikeCount,
            ConvertedTotalLikeCount
        ))
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField
        );
    }

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityLikeConversionStatus::RejectedInvalidNumericField
        );
    }

    FTSLikeInput Input;
    Input.LikeCount = ConvertedLikeCount;
    Input.TotalLikeCount = ConvertedTotalLikeCount;
    Input.User = std::move(*ConvertedUser);

    FTSTikFinityLikeConversionResult Result;
    Result.Status = ETSTikFinityLikeConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
