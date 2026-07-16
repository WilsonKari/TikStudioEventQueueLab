#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include "EventPipeline/Families/TSChatFamily.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatProcessingDispatch>
);

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatDispatchResult>
);

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatProcessingCompletionResult>
);

static_assert(std::is_nothrow_move_constructible_v<FTSConfirmResult>);
static_assert(
    std::is_nothrow_move_constructible_v<FTSCancelInFlightResult>
);
static_assert(std::is_nothrow_move_constructible_v<FTSPumpResult>);
static_assert(
    std::is_nothrow_move_constructible_v<FTSProcessDueExpirationsResult>
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

    enum class EChatLifecycleBatchKind : std::uint8_t
    {
        PendingOnly,
        Confirm,
        Cancel
    };

    struct FValidatedChatLifecycleEntry
    {
        FTSEmissionId EmissionId = 0;
        FTSPayloadHandle PayloadHandle{};
        ETSExternalEmissionState ExpectedState =
            ETSExternalEmissionState::Bound;
    };

    [[nodiscard]]
    constexpr bool IsPendingTerminalReason(
        ETSEmissionTerminalReason Reason
    ) noexcept
    {
        return Reason == ETSEmissionTerminalReason::ExpiredDiscard ||
            Reason == ETSEmissionTerminalReason::ExpiredConsolidate ||
            Reason == ETSEmissionTerminalReason::Evicted;
    }

    [[nodiscard]]
    ETSExternalEmissionState GetExpectedExternalState(
        ETSEmissionTerminalReason Reason
    )
    {
        switch (Reason)
        {
        case ETSEmissionTerminalReason::Confirmed:
        case ETSEmissionTerminalReason::Cancelled:
            return ETSExternalEmissionState::Processing;

        case ETSEmissionTerminalReason::ExpiredDiscard:
        case ETSEmissionTerminalReason::ExpiredConsolidate:
        case ETSEmissionTerminalReason::Evicted:
            return ETSExternalEmissionState::Bound;
        }

        throw std::logic_error("Unsupported Chat lifecycle reason");
    }

    void ValidateChatLifecycleBatchShape(
        const FTSEmissionLifecycleEvents& LifecycleEvents,
        EChatLifecycleBatchKind BatchKind,
        FTSEmissionId RequestedEmissionId
    )
    {
        if (BatchKind == EChatLifecycleBatchKind::Confirm)
        {
            if (LifecycleEvents.empty() ||
                LifecycleEvents.front().Envelope.EmissionId !=
                    RequestedEmissionId ||
                LifecycleEvents.front().Reason !=
                    ETSEmissionTerminalReason::Confirmed)
            {
                throw std::logic_error(
                    "Chat Confirm lifecycle has an invalid leading event"
                );
            }

            for (std::size_t Index = 1;
                 Index < LifecycleEvents.size();
                 ++Index)
            {
                if (!IsPendingTerminalReason(LifecycleEvents[Index].Reason))
                {
                    throw std::logic_error(
                        "Chat Confirm lifecycle has an invalid trailing event"
                    );
                }
            }

            return;
        }

        if (BatchKind == EChatLifecycleBatchKind::Cancel)
        {
            if (LifecycleEvents.size() != 1 ||
                LifecycleEvents.front().Envelope.EmissionId !=
                    RequestedEmissionId ||
                LifecycleEvents.front().Reason !=
                    ETSEmissionTerminalReason::Cancelled)
            {
                throw std::logic_error("Chat Cancel lifecycle is invalid");
            }

            return;
        }

        for (const FTSEmissionLifecycleEvent& LifecycleEvent : LifecycleEvents)
        {
            if (!IsPendingTerminalReason(LifecycleEvent.Reason))
            {
                throw std::logic_error(
                    "Pending Chat lifecycle has a non-pending terminal reason"
                );
            }
        }
    }

    void ValidateAndApplyChatLifecycleBatch(
        FTSEmissionBindingRegistry& BindingRegistry,
        FTSChatPayloadRepository& ChatPayloadRepository,
        const std::optional<FTSEmissionEnvelope>& PendingReadyEmission,
        const FTSEmissionLifecycleEvents& LifecycleEvents,
        EChatLifecycleBatchKind BatchKind,
        FTSEmissionId RequestedEmissionId
    )
    {
        ValidateChatLifecycleBatchShape(
            LifecycleEvents,
            BatchKind,
            RequestedEmissionId
        );

        // Toda la tanda se valida antes de cambiar una autoridad externa.
        std::vector<FValidatedChatLifecycleEntry> ValidatedEntries;
        ValidatedEntries.reserve(LifecycleEvents.size());

        for (const FTSEmissionLifecycleEvent& LifecycleEvent : LifecycleEvents)
        {
            const FTSEmissionId EmissionId =
                LifecycleEvent.Envelope.EmissionId;

            if (EmissionId == 0)
            {
                throw std::logic_error(
                    "Chat lifecycle references an invalid identity"
                );
            }

            if (LifecycleEvent.Envelope.Flow != ETSEventFlow::Chat)
            {
                throw std::logic_error(
                    "Chat lifecycle references a non-Chat flow"
                );
            }

            for (const FValidatedChatLifecycleEntry& Entry : ValidatedEntries)
            {
                if (Entry.EmissionId == EmissionId)
                {
                    throw std::logic_error(
                        "Chat lifecycle contains a duplicate identity"
                    );
                }
            }

            if (PendingReadyEmission.has_value() &&
                PendingReadyEmission->EmissionId == EmissionId)
            {
                throw std::logic_error(
                    "Chat lifecycle targets a pending ready notification"
                );
            }

            const ETSExternalEmissionState ExpectedState =
                GetExpectedExternalState(LifecycleEvent.Reason);
            FTSEmissionBinding Binding;
            const bool bFoundBinding = BindingRegistry.Visit(
                EmissionId,
                [&](const FTSEmissionBinding& StoredBinding)
                {
                    Binding = StoredBinding;
                }
            );

            if (!bFoundBinding)
            {
                throw std::logic_error(
                    "Chat lifecycle references a missing binding"
                );
            }

            if (Binding.EmissionId != EmissionId)
            {
                throw std::logic_error(
                    "Chat lifecycle binding identity mismatch"
                );
            }

            if (Binding.FamilyKind != ETSEventFamilyKind::Chat)
            {
                throw std::logic_error(
                    "Chat lifecycle references a non-Chat binding"
                );
            }

            if (Binding.ExpectedFlow != ETSEventFlow::Chat ||
                Binding.ExpectedFlow != LifecycleEvent.Envelope.Flow)
            {
                throw std::logic_error(
                    "Chat lifecycle binding flow mismatch"
                );
            }

            if (Binding.PayloadHandle.Value == 0)
            {
                throw std::logic_error(
                    "Chat lifecycle binding has an invalid payload handle"
                );
            }

            if (Binding.ExternalState != ExpectedState)
            {
                throw std::logic_error(
                    "Chat lifecycle binding state mismatch"
                );
            }

            const bool bFoundPayload = ChatPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSChatPayload&)
                {
                }
            );

            if (!bFoundPayload)
            {
                throw std::logic_error(
                    "Chat lifecycle binding references a missing payload"
                );
            }

            ValidatedEntries.push_back(
                FValidatedChatLifecycleEntry{
                    EmissionId,
                    Binding.PayloadHandle,
                    ExpectedState
                }
            );
        }

        // La aplicación conserva exactamente el orden comunicado por el core.
        for (const FValidatedChatLifecycleEntry& Entry : ValidatedEntries)
        {
            if (!BindingRegistry.TransitionState(
                    Entry.EmissionId,
                    Entry.ExpectedState,
                    ETSExternalEmissionState::TerminalPendingHandling
                ))
            {
                throw std::logic_error(
                    "Chat lifecycle binding transition failed"
                );
            }

            if (!ChatPayloadRepository.Erase(Entry.PayloadHandle))
            {
                throw std::logic_error(
                    "Chat lifecycle payload disappeared before cleanup"
                );
            }

            if (!BindingRegistry.Erase(Entry.EmissionId))
            {
                throw std::logic_error(
                    "Chat lifecycle binding disappeared before cleanup"
                );
            }
        }
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
        ProcessPendingLifecycleEvents(CoreResult.LifecycleEvents);
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

    ProcessPendingLifecycleEvents(CoreResult.LifecycleEvents);
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

FTSChatProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteChatProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    FTSChatProcessingCompletionResult Result;
    Result.EmissionId = EmissionId;
    Result.ProcessingResult = ProcessingResult;

    if (EmissionId == 0)
    {
        throw std::logic_error(
            "Chat completion references an invalid identity"
        );
    }

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error(
            "Chat completion cannot coexist with a pending ready notification"
        );
    }

    switch (ProcessingResult)
    {
    case ETSProcessingResult::Succeeded:
    case ETSProcessingResult::Cancelled:
    case ETSProcessingResult::Failed:
        break;

    default:
        throw std::logic_error("Chat completion result is invalid");
    }

    // Las autoridades externas se comprueban antes de pedir al core una
    // transición terminal que no puede deshacerse.
    FTSEmissionBinding Binding;
    const bool bFoundBinding = BindingRegistry.Visit(
        EmissionId,
        [&](const FTSEmissionBinding& StoredBinding)
        {
            Binding = StoredBinding;
        }
    );

    if (!bFoundBinding)
    {
        throw std::logic_error("Chat completion has no binding");
    }

    if (Binding.EmissionId != EmissionId)
    {
        throw std::logic_error("Chat completion binding identity mismatch");
    }

    if (Binding.FamilyKind != ETSEventFamilyKind::Chat)
    {
        throw std::logic_error("Chat completion binding family mismatch");
    }

    if (Binding.ExpectedFlow != ETSEventFlow::Chat)
    {
        throw std::logic_error("Chat completion binding flow mismatch");
    }

    if (Binding.ExternalState != ETSExternalEmissionState::Processing)
    {
        throw std::logic_error("Chat completion binding is not Processing");
    }

    if (Binding.PayloadHandle.Value == 0)
    {
        throw std::logic_error(
            "Chat completion binding has an invalid payload handle"
        );
    }

    const bool bFoundPayload = ChatPayloadRepository.Visit(
        Binding.PayloadHandle,
        [](const FTSChatPayload&)
        {
        }
    );

    if (!bFoundPayload)
    {
        throw std::logic_error("Chat completion binding has no payload");
    }

    if (ProcessingResult == ETSProcessingResult::Succeeded)
    {
        FTSConfirmResult CoreResult = Core.Confirm(EmissionId);

        if (CoreResult.Status != ETSConfirmStatus::Confirmed)
        {
            throw std::logic_error("Core rejected a valid Chat confirmation");
        }

        ProcessConfirmLifecycleEvents(
            EmissionId,
            CoreResult.LifecycleEvents
        );
        CaptureCorePumpOutcome(CoreResult.AutoPumpOutcome);
        Result.ConfirmResult.emplace(std::move(CoreResult));
        return Result;
    }

    // Failed también es terminal en este MVP; no existe retry implícito.
    FTSCancelInFlightResult CoreResult = Core.CancelInFlight(EmissionId);

    if (CoreResult.Status != ETSCancelInFlightStatus::Cancelled)
    {
        throw std::logic_error("Core rejected a valid Chat cancellation");
    }

    ProcessCancelLifecycleEvents(
        EmissionId,
        CoreResult.LifecycleEvents
    );
    Result.CancelResult.emplace(std::move(CoreResult));
    return Result;
}

FTSPumpResult FTSEventPipelineCoordinator::Pump()
{
    FTSPumpResult Result = Core.Pump();
    ProcessPendingLifecycleEvents(Result.LifecycleEvents);
    CaptureCorePumpOutcome(Result.Outcome);
    return Result;
}

FTSProcessDueExpirationsResult
FTSEventPipelineCoordinator::ProcessDueExpirations()
{
    FTSProcessDueExpirationsResult Result = Core.ProcessDueExpirations();
    ProcessPendingLifecycleEvents(Result.LifecycleEvents);
    return Result;
}

FTSNextWakeTimeResult FTSEventPipelineCoordinator::GetNextWakeTime()
{
    return Core.GetNextWakeTime();
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

void FTSEventPipelineCoordinator::ProcessPendingLifecycleEvents(
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyChatLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        EChatLifecycleBatchKind::PendingOnly,
        0
    );
}

void FTSEventPipelineCoordinator::ProcessConfirmLifecycleEvents(
    FTSEmissionId EmissionId,
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyChatLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        EChatLifecycleBatchKind::Confirm,
        EmissionId
    );
}

void FTSEventPipelineCoordinator::ProcessCancelLifecycleEvents(
    FTSEmissionId EmissionId,
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyChatLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        EChatLifecycleBatchKind::Cancel,
        EmissionId
    );
}
