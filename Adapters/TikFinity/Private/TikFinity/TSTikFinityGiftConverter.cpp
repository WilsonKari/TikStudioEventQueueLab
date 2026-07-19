#include "TikFinity/TSTikFinityGiftConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityGiftConversionResult MakeRejectedResult(
        ETSTikFinityGiftConversionStatus Status
    )
    {
        FTSTikFinityGiftConversionResult Result;
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
        std::int32_t DefaultValue,
        std::int32_t& Destination
    ) noexcept
    {
        if (!Source.has_value())
        {
            Destination = DefaultValue;
            return true;
        }

        return TryConvertNonNegative(*Source, Destination);
    }
}

FTSTikFinityGiftConversionResult FTSTikFinityGiftConverter::Convert(
    const FTSTikFinityDecodedGiftMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "gift")
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::IgnoredNonGiftEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedGiftData& Data = *Message.Data;
    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;
    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingUserIdentity
        );
    }

    if (!Data.GiftId.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingGiftId
        );
    }

    if (!Data.GiftName.has_value() || Data.GiftName->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingGiftName
        );
    }

    if (!Data.DiamondCount.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedMissingDiamondCount
        );
    }

    std::int32_t ConvertedGiftId = 0;
    std::int32_t ConvertedDiamondCount = 0;
    std::int32_t ConvertedRepeatCount = 1;
    std::int32_t ConvertedGiftType = 0;
    if (!TryConvertNonNegative(*Data.GiftId, ConvertedGiftId) ||
        !TryConvertNonNegative(
            *Data.DiamondCount,
            ConvertedDiamondCount
        ) ||
        !TryConvertOptionalNonNegative(
            Data.RepeatCount,
            1,
            ConvertedRepeatCount
        ) ||
        !TryConvertOptionalNonNegative(
            Data.GiftType,
            0,
            ConvertedGiftType
        ))
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedInvalidNumericField
        );
    }

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityGiftConversionStatus::RejectedInvalidNumericField
        );
    }

    FTSGiftInput Input;
    Input.GiftId = ConvertedGiftId;
    Input.GiftName = *Data.GiftName;
    Input.GiftPictureUrl =
        Data.GiftPictureUrl.value_or(std::string{});
    Input.DiamondCount = ConvertedDiamondCount;
    Input.RepeatCount = ConvertedRepeatCount;
    Input.GiftType = ConvertedGiftType;
    Input.Describe = Data.Describe.value_or(std::string{});
    Input.bRepeatEnd = Data.bRepeatEnd.value_or(false);
    Input.GroupId = Data.GroupId.value_or(std::string{});
    Input.User = std::move(*ConvertedUser);

    FTSTikFinityGiftConversionResult Result;
    Result.Status = ETSTikFinityGiftConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
