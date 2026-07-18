#include "TikFinity/TSTikFinityShareConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityShareConversionResult MakeRejectedResult(
        ETSTikFinityShareConversionStatus Status
    )
    {
        FTSTikFinityShareConversionResult Result;
        Result.Status = Status;
        return Result;
    }
}

FTSTikFinityShareConversionResult FTSTikFinityShareConverter::Convert(
    const FTSTikFinityDecodedShareMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "share")
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::IgnoredNonShareEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedShareData& Data = *Message.Data;
    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;
    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::RejectedMissingUserIdentity
        );
    }

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityShareConversionStatus::RejectedInvalidNumericField
        );
    }

    FTSShareInput Input;
    Input.User = std::move(*ConvertedUser);

    FTSTikFinityShareConversionResult Result;
    Result.Status = ETSTikFinityShareConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
