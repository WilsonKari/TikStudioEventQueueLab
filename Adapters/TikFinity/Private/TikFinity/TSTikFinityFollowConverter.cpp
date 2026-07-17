#include "TikFinity/TSTikFinityFollowConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityFollowConversionResult MakeRejectedResult(
        ETSTikFinityFollowConversionStatus Status
    )
    {
        FTSTikFinityFollowConversionResult Result;
        Result.Status = Status;
        return Result;
    }
}

FTSTikFinityFollowConversionResult FTSTikFinityFollowConverter::Convert(
    const FTSTikFinityDecodedFollowMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "follow")
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::IgnoredNonFollowEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedFollowData& Data = *Message.Data;

    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;

    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::RejectedMissingUserIdentity
        );
    }

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityFollowConversionStatus::RejectedInvalidNumericField
        );
    }

    FTSFollowInput Input;
    Input.User = std::move(*ConvertedUser);

    FTSTikFinityFollowConversionResult Result;
    Result.Status = ETSTikFinityFollowConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
