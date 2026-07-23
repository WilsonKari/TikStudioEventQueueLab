#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <chrono>
#include <stdexcept>
#include <type_traits>

class FTSScenarioClock final
{
public:
    [[nodiscard]]
    FTSNowProvider MakeProvider()
    {
        return [this]() noexcept
        {
            return Now;
        };
    }

    [[nodiscard]]
    FTSEventQueueTimePoint GetOrigin() const noexcept
    {
        return Origin;
    }

    [[nodiscard]]
    FTSEventQueueTimePoint GetNow() const noexcept
    {
        return Now;
    }

    [[nodiscard]]
    std::chrono::milliseconds GetOffset() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Now - Origin
        );
    }

    void AdvanceTo(std::chrono::milliseconds Offset)
    {
        if (Offset.count() < 0)
        {
            throw std::invalid_argument(
                "Scenario clock offset cannot be negative"
            );
        }

        using FClockDuration = FTSEventQueueTimePoint::duration;
        using FWideDuration = std::common_type_t<
            std::chrono::duration<long double, FClockDuration::period>,
            std::chrono::duration<long double, std::milli>
        >;

        if (FWideDuration{Offset} > FWideDuration{FClockDuration::max()})
        {
            throw std::overflow_error(
                "Scenario clock offset exceeds the clock range"
            );
        }

        const FClockDuration ClockOffset =
            std::chrono::ceil<FClockDuration>(Offset);
        const FTSEventQueueTimePoint Requested{ClockOffset};
        if (Requested < Now)
        {
            throw std::logic_error("Scenario clock cannot move backwards");
        }

        Now = Requested;
    }

private:
    FTSEventQueueTimePoint Origin{};
    FTSEventQueueTimePoint Now{};
};
