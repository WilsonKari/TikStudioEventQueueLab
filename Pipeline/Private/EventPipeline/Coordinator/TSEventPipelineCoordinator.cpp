#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include "EventPipeline/Families/TSChatFamily.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatProcessingDispatch>
);

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatDispatchResult>
);

namespace
{
    class FProvisionalChatPayloadGuard final
    {
    public:
        FProvisionalChatPayloadGuard(
            FTSChatPayloadRepository& Repository,
            FTSPayloadHandle PayloadHandle
        ) noexcept
            : Repository(Repository)
            , PayloadHandle(PayloadHandle)
        {
        }

        ~FProvisionalChatPayloadGuard() noexcept
        {
            if (!bActive)
            {
                return;
            }

            try
            {
                const bool bErased = Repository.Erase(PayloadHandle);
                (void)bErased;
            }
            catch (...)
            {
                // A cleanup guard must never replace the exception already in flight.
            }
        }

        FProvisionalChatPayloadGuard(
            const FProvisionalChatPayloadGuard&
        ) = delete;
        FProvisionalChatPayloadGuard& operator=(
            const FProvisionalChatPayloadGuard&
        ) = delete;
        FProvisionalChatPayloadGuard(FProvisionalChatPayloadGuard&&) = delete;
        FProvisionalChatPayloadGuard& operator=(
            FProvisionalChatPayloadGuard&&
        ) = delete;

        void RollbackNow()
        {
            if (!bActive)
            {
                return;
            }

            if (!Repository.Erase(PayloadHandle))
            {
                throw std::logic_error(
                    "Provisional Chat payload is missing during rollback"
                );
            }

            bActive = false;
        }

        void Release() noexcept
        {
            bActive = false;
        }

    private:
        FTSChatPayloadRepository& Repository;
        FTSPayloadHandle PayloadHandle{};
        bool bActive = true;
    };

    [[nodiscard]]
    constexpr bool IsAcceptedStatus(ETSEnqueueStatus Status) noexcept
    {
        return Status == ETSEnqueueStatus::Accepted ||
            Status == ETSEnqueueStatus::AcceptedWithEviction;
    }

    [[nodiscard]]
    constexpr bool IsSupportedEnqueueTerminalReason(
        ETSEmissionTerminalReason Reason
    ) noexcept
    {
        return Reason == ETSEmissionTerminalReason::ExpiredDiscard ||
            Reason == ETSEmissionTerminalReason::ExpiredConsolidate ||
            Reason == ETSEmissionTerminalReason::Evicted;
    }
}

FTSEventPipelineCoordinator::FTSEventPipelineCoordinator(
    FTSEventQueueSettings Settings,
    FTSNowProvider NowProvider
)
    : Core(std::move(Settings), std::move(NowProvider))
{
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitChat(
    FTSChatInput Input
)
{
    FTSPipelineAdmissionResult Result;
    TTSFamilyDecision<FTSChatPayload> Decision =
        FTSChatFamily::Decide(std::move(Input));

    if (!Decision.has_value())
    {
        return Result;
    }

    TTSAdmissionCandidate<FTSChatPayload> Candidate =
        std::move(*Decision);
    const std::optional<FTSPayloadHandle> PayloadHandle =
        ChatPayloadRepository.Insert(std::move(Candidate.Payload));

    if (!PayloadHandle.has_value())
    {
        Result.Status =
            ETSPipelineAdmissionStatus::RejectedPayloadIdentityExhausted;
        return Result;
    }

    FProvisionalChatPayloadGuard ProvisionalGuard(
        ChatPayloadRepository,
        *PayloadHandle
    );
    FTSEnqueueResult CoreResult = Core.Enqueue(Candidate.EnqueueRequest);

    if (!IsAcceptedStatus(CoreResult.Status))
    {
        ProcessEnqueueLifecycleEvents(CoreResult.LifecycleEvents);
        ProvisionalGuard.RollbackNow();

        Result.Status = ETSPipelineAdmissionStatus::RejectedByCore;
        Result.EnqueueResult = std::move(CoreResult);
        return Result;
    }

    // El core ya comprometió la emisión; desde aquí ningún fallo debe fingir rollback.
    ProvisionalGuard.Release();

    if (CoreResult.AdmittedEmission.EmissionId == 0)
    {
        throw std::logic_error("Accepted Chat emission has an invalid identity");
    }

    if (CoreResult.AdmittedEmission.Flow != Candidate.EnqueueRequest.Flow)
    {
        throw std::logic_error("Accepted Chat emission flow does not match its request");
    }

    FTSEmissionBinding Binding;
    Binding.EmissionId = CoreResult.AdmittedEmission.EmissionId;
    Binding.FamilyKind = Candidate.FamilyKind;
    Binding.ExpectedFlow = Candidate.EnqueueRequest.Flow;
    Binding.PayloadHandle = *PayloadHandle;
    Binding.ExternalState = ETSExternalEmissionState::Bound;

    if (!BindingRegistry.Insert(std::move(Binding)))
    {
        throw std::logic_error("Accepted Chat emission could not be bound");
    }

    ProcessEnqueueLifecycleEvents(CoreResult.LifecycleEvents);
    CaptureCorePumpOutcome(CoreResult.AutoPumpOutcome);

    Result.Status = ETSPipelineAdmissionStatus::Accepted;
    Result.EnqueueResult = std::move(CoreResult);
    return Result;
}

FTSChatDispatchResult FTSEventPipelineCoordinator::BeginChatProcessing()
{
    FTSChatDispatchResult Result;

    if (!PendingReadyEmission.has_value())
    {
        return Result;
    }

    const FTSEmissionEnvelope ReadyEmission = *PendingReadyEmission;

    if (ReadyEmission.EmissionId == 0)
    {
        throw std::logic_error("Pending Chat ready has an invalid identity");
    }

    if (ReadyEmission.Flow != ETSEventFlow::Chat)
    {
        throw std::logic_error("Pending ready is not a Chat emission");
    }

    FTSEmissionBinding Binding;
    const bool bFoundBinding = BindingRegistry.Visit(
        ReadyEmission.EmissionId,
        [&](const FTSEmissionBinding& StoredBinding)
        {
            Binding = StoredBinding;
        }
    );

    if (!bFoundBinding)
    {
        throw std::logic_error("Pending Chat ready has no binding");
    }

    if (Binding.EmissionId != ReadyEmission.EmissionId)
    {
        throw std::logic_error("Pending Chat ready binding identity mismatch");
    }

    if (Binding.FamilyKind != ETSEventFamilyKind::Chat)
    {
        throw std::logic_error("Pending Chat ready binding family mismatch");
    }

    if (Binding.ExpectedFlow != ReadyEmission.Flow)
    {
        throw std::logic_error("Pending Chat ready binding flow mismatch");
    }

    if (Binding.ExternalState != ETSExternalEmissionState::Bound)
    {
        throw std::logic_error("Pending Chat ready binding is not Bound");
    }

    FTSChatPayload Payload;
    const bool bFoundPayload = ChatPayloadRepository.Visit(
        Binding.PayloadHandle,
        [&](const FTSChatPayload& StoredPayload)
        {
            Payload = StoredPayload;
        }
    );

    if (!bFoundPayload)
    {
        throw std::logic_error("Pending Chat ready binding has no payload");
    }

    FTSChatProcessingDispatch Dispatch{ReadyEmission, Payload};
    Result.Status = ETSPipelineDispatchStatus::Dispatched;
    Result.Dispatch.emplace(std::move(Dispatch));

    // Toda copia potencialmente lanzable terminó; desde aquí sólo hay compromiso.
    if (!BindingRegistry.TransitionState(
            Binding.EmissionId,
            ETSExternalEmissionState::Bound,
            ETSExternalEmissionState::Processing
        ))
    {
        throw std::logic_error("Pending Chat ready could not enter Processing");
    }

    PendingReadyEmission.reset();
    return Result;
}

std::size_t FTSEventPipelineCoordinator::GetBindingCount() const noexcept
{
    return BindingRegistry.Size();
}

std::size_t FTSEventPipelineCoordinator::GetChatPayloadCount() const noexcept
{
    return ChatPayloadRepository.Size();
}

void FTSEventPipelineCoordinator::CaptureCorePumpOutcome(
    const FTSPumpOutcome& PumpOutcome
)
{
    if (PumpOutcome.Status != ETSPumpStatus::EmissionReady)
    {
        return;
    }

    const FTSEmissionEnvelope& ReadyEmission = PumpOutcome.ReadyEmission;

    if (ReadyEmission.EmissionId == 0)
    {
        throw std::logic_error("Core produced a Chat ready with an invalid identity");
    }

    if (ReadyEmission.Flow != ETSEventFlow::Chat)
    {
        throw std::logic_error("Core produced a non-Chat ready during Chat admission");
    }

    FTSEmissionBinding Binding;
    const bool bFoundBinding = BindingRegistry.Visit(
        ReadyEmission.EmissionId,
        [&](const FTSEmissionBinding& StoredBinding)
        {
            Binding = StoredBinding;
        }
    );

    if (!bFoundBinding)
    {
        throw std::logic_error("Core Chat ready has no binding");
    }

    if (Binding.EmissionId != ReadyEmission.EmissionId)
    {
        throw std::logic_error("Core Chat ready binding identity mismatch");
    }

    if (Binding.FamilyKind != ETSEventFamilyKind::Chat)
    {
        throw std::logic_error("Core Chat ready binding family mismatch");
    }

    if (Binding.ExpectedFlow != ReadyEmission.Flow)
    {
        throw std::logic_error("Core Chat ready binding flow mismatch");
    }

    if (Binding.ExternalState != ETSExternalEmissionState::Bound)
    {
        throw std::logic_error("Core Chat ready binding is not Bound");
    }

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error("A Chat ready notification is already pending");
    }

    PendingReadyEmission = ReadyEmission;
}

void FTSEventPipelineCoordinator::ProcessEnqueueLifecycleEvents(
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    for (const FTSEmissionLifecycleEvent& LifecycleEvent : LifecycleEvents)
    {
        if (!IsSupportedEnqueueTerminalReason(LifecycleEvent.Reason))
        {
            throw std::logic_error("Unsupported lifecycle reason during Chat admission");
        }

        FTSEmissionBinding Binding;
        const bool bFoundBinding = BindingRegistry.Visit(
            LifecycleEvent.Envelope.EmissionId,
            [&](const FTSEmissionBinding& StoredBinding)
            {
                Binding = StoredBinding;
            }
        );

        if (!bFoundBinding)
        {
            throw std::logic_error("Lifecycle event references a missing binding");
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Chat)
        {
            throw std::logic_error("Lifecycle event references a non-Chat binding");
        }

        if (Binding.ExpectedFlow != LifecycleEvent.Envelope.Flow)
        {
            throw std::logic_error("Lifecycle event flow does not match its binding");
        }

        if (Binding.ExternalState != ETSExternalEmissionState::Bound)
        {
            throw std::logic_error("Lifecycle event binding is not Bound");
        }

        if (!BindingRegistry.TransitionState(
                Binding.EmissionId,
                ETSExternalEmissionState::Bound,
                ETSExternalEmissionState::TerminalPendingHandling
            ))
        {
            throw std::logic_error("Lifecycle binding transition failed");
        }

        if (!ChatPayloadRepository.Erase(Binding.PayloadHandle))
        {
            throw std::logic_error("Lifecycle binding references a missing Chat payload");
        }

        if (!BindingRegistry.Erase(Binding.EmissionId))
        {
            throw std::logic_error("Lifecycle binding disappeared before cleanup");
        }
    }
}
