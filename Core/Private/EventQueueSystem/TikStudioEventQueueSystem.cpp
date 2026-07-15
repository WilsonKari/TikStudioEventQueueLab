#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <limits>
#include <memory>
#include <unordered_map>
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

    enum class EInternalEmissionState : std::uint8_t
    {
        Pending
    };

    struct FInternalEmissionRecord
    {
        FTSEmissionEnvelope Envelope{};
        EInternalEmissionState State = EInternalEmissionState::Pending;
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard;
        bool bProtectedFromEviction = false;
        std::uint64_t Revision = 1;
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

    [[nodiscard]]
    bool IsFlowAtCapacity(
        ETSEventFlow Flow,
        std::uint32_t MaxSlots
    ) const noexcept
    {
        if (MaxSlots == 0)
        {
            return true;
        }

        std::size_t LiveCount = 0;

        for (const auto& Entry : Records)
        {
            if (Entry.second.Envelope.Flow != Flow)
            {
                continue;
            }

            ++LiveCount;

            if (LiveCount >= static_cast<std::size_t>(MaxSlots))
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]]
    static FTSEventQueueTimePoint CalculateExpiresAt(
        FTSEventQueueTimePoint Now,
        std::chrono::milliseconds EffectiveTTL
    ) noexcept
    {
        using FClockDuration = FTSEventQueueTimePoint::duration;

        const FTSEventQueueTimePoint MaxTimePoint =
            FTSEventQueueTimePoint::max();

        if (EffectiveTTL.count() == 0)
        {
            return MaxTimePoint;
        }

        const std::chrono::milliseconds MaxDurationInMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                FClockDuration::max()
            );

        if (EffectiveTTL > MaxDurationInMilliseconds)
        {
            return MaxTimePoint;
        }

        const FClockDuration TTLDuration =
            std::chrono::duration_cast<FClockDuration>(EffectiveTTL);
        const FClockDuration NowDuration = Now.time_since_epoch();

        if (
            NowDuration > FClockDuration::zero()
            && TTLDuration > FClockDuration::max() - NowDuration
        )
        {
            return MaxTimePoint;
        }

        return FTSEventQueueTimePoint{NowDuration + TTLDuration};
    }

    FTSEventQueueSettings Settings;
    FTSNowProvider NowProvider;
    FTSEmissionId NextEmissionId = 1;
    FTSEmissionSequence NextSequence = 1;
    std::unordered_map<FTSEmissionId, FInternalEmissionRecord> Records;

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

FTSEnqueueResult TikStudioEventQueueSystem::Enqueue(
    const FTSEnqueueRequest& Request
)
{
    FTSEnqueueResult Result;
    const FImpl::FAdmissionValidationResult Validation =
        Impl->ValidateEnqueueRequest(Request);

    switch (Validation.Status)
    {
    case FImpl::EAdmissionValidationStatus::InvalidFlow:
        Result.Status = ETSEnqueueStatus::RejectedInvalidFlow;
        return Result;

    case FImpl::EAdmissionValidationStatus::Disabled:
        Result.Status = ETSEnqueueStatus::RejectedDisabled;
        return Result;

    case FImpl::EAdmissionValidationStatus::InvalidTTL:
        Result.Status = ETSEnqueueStatus::RejectedInvalidTTL;
        return Result;

    case FImpl::EAdmissionValidationStatus::Valid:
        break;
    }

    const FTSFlowQueueSettings& FlowSettings = *Validation.FlowSettings;

    if (Impl->IsFlowAtCapacity(Request.Flow, FlowSettings.MaxSlots))
    {
        Result.Status = ETSEnqueueStatus::RejectedAtCapacity;
        return Result;
    }

    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    const std::int64_t PriorityScore = FImpl::CalculatePriorityScore(
        FlowSettings.BaseWeight,
        Request.PriorityAdjustment
    );
    const FTSEventQueueTimePoint ExpiresAt = FImpl::CalculateExpiresAt(
        Now,
        Validation.EffectiveTTL
    );

    FImpl::FEmissionIdentity Identity;

    if (!Impl->TryAllocateEmissionIdentity(Identity))
    {
        Result.Status = ETSEnqueueStatus::RejectedIdentityExhausted;
        return Result;
    }

    FTSEmissionEnvelope Envelope;
    Envelope.EmissionId = Identity.EmissionId;
    Envelope.Flow = Request.Flow;
    Envelope.Sequence = Identity.Sequence;
    Envelope.CreatedAt = Now;
    Envelope.ExpiresAt = ExpiresAt;
    Envelope.PriorityScore = PriorityScore;

    FImpl::FInternalEmissionRecord Record;
    Record.Envelope = Envelope;
    Record.State = FImpl::EInternalEmissionState::Pending;
    Record.ExpirePolicy = FlowSettings.ExpirePolicy;
    Record.bProtectedFromEviction =
        Request.bProtectedFromEviction
        || FlowSettings.bExemptFromEviction;
    Record.Revision = 1;

    Impl->Records.emplace(Envelope.EmissionId, std::move(Record));

    Result.Status = ETSEnqueueStatus::Accepted;
    Result.AdmittedEmission = Envelope;
    return Result;
}

TikStudioEventQueueSystem::~TikStudioEventQueueSystem() = default;

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    TikStudioEventQueueSystem&&
) noexcept = default;

TikStudioEventQueueSystem& TikStudioEventQueueSystem::operator=(
    TikStudioEventQueueSystem&&
) noexcept = default;
