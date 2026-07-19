#include "TikFinity/TSTikFinityMemberConverter.h"

#include "TikFinity/TSTikFinityDecodedUserConverter.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace
{
    [[nodiscard]]
    FTSTikFinityMemberConversionResult MakeRejectedResult(
        ETSTikFinityMemberConversionStatus Status
    )
    {
        FTSTikFinityMemberConversionResult Result;
        Result.Status = Status;
        return Result;
    }
}

FTSTikFinityMemberConversionResult FTSTikFinityMemberConverter::Convert(
    const FTSTikFinityDecodedMemberMessage& Message
)
{
    if (Message.EventName.empty())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedInvalidEnvelope
        );
    }

    if (Message.EventName != "member")
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::IgnoredNonMemberEvent
        );
    }

    if (!Message.Data.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedMissingData
        );
    }

    const FTSTikFinityDecodedMemberData& Data = *Message.Data;
    if (!Data.User.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedMissingUser
        );
    }

    const FTSTikFinityDecodedUser& User = *Data.User;
    if (!User.UniqueId.has_value() || User.UniqueId->empty())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedMissingUserIdentity
        );
    }

    if (!Data.ActionId.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedMissingActionId
        );
    }

    if (*Data.ActionId < 0 ||
        *Data.ActionId > std::numeric_limits<std::int32_t>::max())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedInvalidNumericField
        );
    }

    std::optional<FTSUserSnapshot> ConvertedUser =
        FTSTikFinityDecodedUserConverter::TryConvert(User);
    if (!ConvertedUser.has_value())
    {
        return MakeRejectedResult(
            ETSTikFinityMemberConversionStatus::RejectedInvalidNumericField
        );
    }

    FTSMemberInput Input;
    Input.ActionId = static_cast<std::int32_t>(*Data.ActionId);
    Input.User = std::move(*ConvertedUser);

    FTSTikFinityMemberConversionResult Result;
    Result.Status = ETSTikFinityMemberConversionStatus::Converted;
    Result.Input.emplace(std::move(Input));
    return Result;
}
