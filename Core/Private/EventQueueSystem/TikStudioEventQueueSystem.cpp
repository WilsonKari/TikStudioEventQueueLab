#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <limits>
#include <memory>
#include <utility>

class TikStudioEventQueueSystem::FImpl final
{
public:
    struct FEmissionIdentity
    {
        FTSEmissionId EmissionId = 0;
        FTSEmissionSequence Sequence = 0;
    };

    enum class EAdmissionValidationStatus : std::uint8_t
    {
        Valid,
        InvalidFlow,
        Disabled,
        InvalidTTL
    };

    struct FAdmissionValidationResult
    {
        EAdmissionValidationStatus Status =
            EAdmissionValidationStatus::InvalidFlow;
        const FTSFlowQueueSettings* FlowSettings = nullptr;
        std::chrono::milliseconds EffectiveTTL{0};
    };

    explicit FImpl(
        FTSEventQueueSettings InSettings,
        FTSNowProvider InNowProvider
    )
        : Settings(std::move(InSettings))
        , NowProvider(std::move(InNowProvider))
    {
        if (!NowProvider)
        {
            NowProvider = []() noexcept
            {
                return FTSEventQueueClock::now();
            };
        }
    }

    [[nodiscard]]
    bool TryAllocateEmissionIdentity(
        FEmissionIdentity& OutIdentity
    ) noexcept
    {
        if (NextEmissionId == 0 || NextSequence == 0)
        {
            return false;
        }

        OutIdentity.EmissionId = NextEmissionId;
        OutIdentity.Sequence = NextSequence;

        AdvanceCounterOrMarkExhausted(NextEmissionId);
        AdvanceCounterOrMarkExhausted(NextSequence);

        return true;
    }

    [[nodiscard]]
    FAdmissionValidationResult ValidateEnqueueRequest(
        const FTSEnqueueRequest& Request
    ) const noexcept
    {
        FAdmissionValidationResult Result;

        if (!IsValidFlow(Request.Flow))
        {
            return Result;
        }

        const FTSFlowQueueSettings* ResolvedFlowSettings =
            Settings.TryGetFlowSettings(Request.Flow);

        if (ResolvedFlowSettings == nullptr)
        {
            return Result;
        }

        Result.FlowSettings = ResolvedFlowSettings;
        Result.EffectiveTTL = Request.bOverrideTTL
            ? Request.TTLOverride
            : ResolvedFlowSettings->TTL;

        if (!ResolvedFlowSettings->bEnabled)
        {
            Result.Status = EAdmissionValidationStatus::Disabled;
            return Result;
        }

        if (Result.EffectiveTTL.count() < 0)
        {
            Result.Status = EAdmissionValidationStatus::InvalidTTL;
            return Result;
        }

        Result.Status = EAdmissionValidationStatus::Valid;
        return Result;
    }

    [[nodiscard]]
    static std::int64_t CalculatePriorityScore(
        std::int32_t BaseWeight,
        std::int64_t PriorityAdjustment
    ) noexcept
    {
        const std::int64_t BaseWeight64 =
            static_cast<std::int64_t>(BaseWeight);
        constexpr std::int64_t MinValue =
            std::numeric_limits<std::int64_t>::min();
        constexpr std::int64_t MaxValue =
            std::numeric_limits<std::int64_t>::max();

        if (BaseWeight64 > 0 && PriorityAdjustment > MaxValue - BaseWeight64)
        {
            return MaxValue;
        }

        if (BaseWeight64 < 0 && PriorityAdjustment < MinValue - BaseWeight64)
        {
            return MinValue;
        }

        return PriorityAdjustment + BaseWeight64;
    }

    [[nodiscard]]
    FTSEventQueueTimePoint CaptureNow() const
    {
        return NowProvider();
    }

    FTSEventQueueSettings Settings;
    FTSNowProvider NowProvider;
    FTSEmissionId NextEmissionId = 1;
    FTSEmissionSequence NextSequence = 1;

private:
    static void AdvanceCounterOrMarkExhausted(
        std::uint64_t& Counter
    ) noexcept
    {
        if (Counter == std::numeric_limits<std::uint64_t>::max())
        {
            Counter = 0;
            return;
        }

        ++Counter;
    }
};

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    FTSEventQueueSettings Settings,
    FTSNowProvider NowProvider
)
    : Impl(std::make_unique<FImpl>(
        std::move(Settings),
        std::move(NowProvider)
    ))
{
}

TikStudioEventQueueSystem::~TikStudioEventQueueSystem() = default;

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    TikStudioEventQueueSystem&&
) noexcept = default;

TikStudioEventQueueSystem& TikStudioEventQueueSystem::operator=(
    TikStudioEventQueueSystem&&
) noexcept = default;
