#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Families/TSFollowFamily.h"
#include "EventPipeline/Families/TSShareFamily.h"
#include "EventPipeline/Families/TSLikeFamily.h"
#include "EventPipeline/Families/TSRoomUserFamily.h"
#include "EventPipeline/Families/TSGiftFamily.h"
#include "EventPipeline/Families/TSMemberFamily.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSFollowProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSShareProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSLikeProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSRoomUserProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSGiftProcessingDispatch>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSMemberProcessingDispatch>
);
static_assert(std::is_nothrow_move_constructible_v<FTSChatDispatchResult>);
static_assert(std::is_nothrow_move_constructible_v<FTSFollowDispatchResult>);
static_assert(
    std::is_nothrow_move_constructible_v<FTSShareDispatchResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSLikeDispatchResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSRoomUserDispatchResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSGiftDispatchResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSMemberDispatchResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSProcessingCompletionResult>
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
    template <typename TRepository>
    class TProvisionalPayloadGuard final
    {
    public:
        TProvisionalPayloadGuard(
            TRepository& Repository,
            FTSPayloadHandle PayloadHandle
        ) noexcept
            : Repository(Repository)
            , PayloadHandle(PayloadHandle)
        {
        }

        ~TProvisionalPayloadGuard() noexcept
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
                // La guarda no debe sustituir una excepción que ya está propagándose.
            }
        }

        TProvisionalPayloadGuard(
            const TProvisionalPayloadGuard&
        ) = delete;
        TProvisionalPayloadGuard& operator=(
            const TProvisionalPayloadGuard&
        ) = delete;
        TProvisionalPayloadGuard(TProvisionalPayloadGuard&&) = delete;
        TProvisionalPayloadGuard& operator=(
            TProvisionalPayloadGuard&&
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
                    "Provisional payload is missing during rollback"
                );
            }

            bActive = false;
        }

        void Release() noexcept
        {
            bActive = false;
        }

    private:
        TRepository& Repository;
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
    constexpr bool IsSupportedFamilyFlowPair(
        ETSEventFamilyKind FamilyKind,
        ETSEventFlow Flow
    ) noexcept
    {
        return
            (FamilyKind == ETSEventFamilyKind::Chat &&
                Flow == ETSEventFlow::Chat) ||
            (FamilyKind == ETSEventFamilyKind::Follow &&
                Flow == ETSEventFlow::Follow) ||
            (FamilyKind == ETSEventFamilyKind::Share &&
                Flow == ETSEventFlow::Share) ||
            (FamilyKind == ETSEventFamilyKind::Like &&
                Flow == ETSEventFlow::Like) ||
            (FamilyKind == ETSEventFamilyKind::RoomUser &&
                Flow == ETSEventFlow::RoomUser) ||
            (FamilyKind == ETSEventFamilyKind::Gift &&
                Flow == ETSEventFlow::Gift) ||
            (FamilyKind == ETSEventFamilyKind::Member &&
                Flow == ETSEventFlow::MemberIdentity);
    }

    void ValidateSupportedFamilyFlowPair(
        ETSEventFamilyKind FamilyKind,
        ETSEventFlow Flow
    )
    {
        if (!IsSupportedFamilyFlowPair(FamilyKind, Flow))
        {
            throw std::logic_error(
                "Pipeline binding has an unsupported family and flow pair"
            );
        }
    }

    enum class ELifecycleBatchKind : std::uint8_t
    {
        PendingOnly,
        Confirm,
        Cancel
    };

    struct FValidatedLifecycleEntry
    {
        FTSEmissionId EmissionId = 0;
        ETSEventFamilyKind FamilyKind = ETSEventFamilyKind::Chat;
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

        throw std::logic_error("Unsupported lifecycle reason");
    }

    void ValidateLifecycleBatchShape(
        const FTSEmissionLifecycleEvents& LifecycleEvents,
        ELifecycleBatchKind BatchKind,
        FTSEmissionId RequestedEmissionId
    )
    {
        if (BatchKind == ELifecycleBatchKind::Confirm)
        {
            if (LifecycleEvents.empty() ||
                LifecycleEvents.front().Envelope.EmissionId !=
                    RequestedEmissionId ||
                LifecycleEvents.front().Reason !=
                    ETSEmissionTerminalReason::Confirmed)
            {
                throw std::logic_error(
                    "Confirm lifecycle has an invalid leading event"
                );
            }

            for (std::size_t Index = 1;
                 Index < LifecycleEvents.size();
                 ++Index)
            {
                if (!IsPendingTerminalReason(LifecycleEvents[Index].Reason))
                {
                    throw std::logic_error(
                        "Confirm lifecycle has an invalid trailing event"
                    );
                }
            }

            return;
        }

        if (BatchKind == ELifecycleBatchKind::Cancel)
        {
            if (LifecycleEvents.size() != 1 ||
                LifecycleEvents.front().Envelope.EmissionId !=
                    RequestedEmissionId ||
                LifecycleEvents.front().Reason !=
                    ETSEmissionTerminalReason::Cancelled)
            {
                throw std::logic_error("Cancel lifecycle is invalid");
            }

            return;
        }

        for (const FTSEmissionLifecycleEvent& LifecycleEvent : LifecycleEvents)
        {
            if (!IsPendingTerminalReason(LifecycleEvent.Reason))
            {
                throw std::logic_error(
                    "Pending lifecycle has a non-pending terminal reason"
                );
            }
        }
    }

    [[nodiscard]]
    bool HasPayloadForBinding(
        const FTSEmissionBinding& Binding,
        const FTSChatPayloadRepository& ChatPayloadRepository,
        const FTSFollowPayloadRepository& FollowPayloadRepository,
        const FTSSharePayloadRepository& SharePayloadRepository,
        const FTSLikePayloadRepository& LikePayloadRepository,
        const FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        const FTSGiftPayloadRepository& GiftPayloadRepository,
        const FTSMemberPayloadRepository& MemberPayloadRepository
    )
    {
        switch (Binding.FamilyKind)
        {
        case ETSEventFamilyKind::Chat:
            return ChatPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSChatPayload&)
                {
                }
            );

        case ETSEventFamilyKind::Follow:
            return FollowPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSFollowPayload&)
                {
                }
            );

        case ETSEventFamilyKind::Share:
            return SharePayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSSharePayload&)
                {
                }
            );

        case ETSEventFamilyKind::Like:
            return LikePayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSLikePayload&)
                {
                }
            );

        case ETSEventFamilyKind::RoomUser:
            return RoomUserPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSRoomUserPayload&)
                {
                }
            );

        case ETSEventFamilyKind::Gift:
            return GiftPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSGiftPayload&)
                {
                }
            );

        case ETSEventFamilyKind::Member:
            return MemberPayloadRepository.Visit(
                Binding.PayloadHandle,
                [](const FTSMemberPayload&)
                {
                }
            );

        default:
            throw std::logic_error(
                "Lifecycle references an unsupported payload family"
            );
        }
    }

    [[nodiscard]]
    bool ErasePayloadForBinding(
        const FValidatedLifecycleEntry& Entry,
        FTSChatPayloadRepository& ChatPayloadRepository,
        FTSFollowPayloadRepository& FollowPayloadRepository,
        FTSSharePayloadRepository& SharePayloadRepository,
        FTSLikePayloadRepository& LikePayloadRepository,
        FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        FTSGiftPayloadRepository& GiftPayloadRepository,
        FTSMemberPayloadRepository& MemberPayloadRepository
    )
    {
        switch (Entry.FamilyKind)
        {
        case ETSEventFamilyKind::Chat:
            return ChatPayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::Follow:
            return FollowPayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::Share:
            return SharePayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::Like:
            return LikePayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::RoomUser:
            return RoomUserPayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::Gift:
            return GiftPayloadRepository.Erase(Entry.PayloadHandle);

        case ETSEventFamilyKind::Member:
            return MemberPayloadRepository.Erase(Entry.PayloadHandle);

        default:
            throw std::logic_error(
                "Lifecycle cleanup references an unsupported payload family"
            );
        }
    }

    void ValidateAndApplyLifecycleBatch(
        FTSEmissionBindingRegistry& BindingRegistry,
        FTSChatPayloadRepository& ChatPayloadRepository,
        FTSFollowPayloadRepository& FollowPayloadRepository,
        FTSSharePayloadRepository& SharePayloadRepository,
        FTSLikePayloadRepository& LikePayloadRepository,
        FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        FTSGiftPayloadRepository& GiftPayloadRepository,
        FTSMemberPayloadRepository& MemberPayloadRepository,
        const std::optional<FTSEmissionEnvelope>& PendingReadyEmission,
        const FTSEmissionLifecycleEvents& LifecycleEvents,
        ELifecycleBatchKind BatchKind,
        FTSEmissionId RequestedEmissionId
    )
    {
        ValidateLifecycleBatchShape(
            LifecycleEvents,
            BatchKind,
            RequestedEmissionId
        );

        // Toda la tanda se valida antes de cambiar cualquiera de sus autoridades.
        std::vector<FValidatedLifecycleEntry> ValidatedEntries;
        ValidatedEntries.reserve(LifecycleEvents.size());

        for (const FTSEmissionLifecycleEvent& LifecycleEvent : LifecycleEvents)
        {
            const FTSEmissionId EmissionId =
                LifecycleEvent.Envelope.EmissionId;

            if (EmissionId == 0)
            {
                throw std::logic_error(
                    "Lifecycle references an invalid identity"
                );
            }

            for (const FValidatedLifecycleEntry& Entry : ValidatedEntries)
            {
                if (Entry.EmissionId == EmissionId)
                {
                    throw std::logic_error(
                        "Lifecycle contains a duplicate identity"
                    );
                }
            }

            if (PendingReadyEmission.has_value() &&
                PendingReadyEmission->EmissionId == EmissionId)
            {
                throw std::logic_error(
                    "Lifecycle targets a pending ready notification"
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
                    "Lifecycle references a missing binding"
                );
            }

            if (Binding.EmissionId != EmissionId)
            {
                throw std::logic_error(
                    "Lifecycle binding identity mismatch"
                );
            }

            if (Binding.ExpectedFlow != LifecycleEvent.Envelope.Flow)
            {
                throw std::logic_error("Lifecycle binding flow mismatch");
            }

            ValidateSupportedFamilyFlowPair(
                Binding.FamilyKind,
                Binding.ExpectedFlow
            );

            if (Binding.ExternalState != ExpectedState)
            {
                throw std::logic_error("Lifecycle binding state mismatch");
            }

            if (Binding.PayloadHandle.Value == 0)
            {
                throw std::logic_error(
                    "Lifecycle binding has an invalid payload handle"
                );
            }

            if (!HasPayloadForBinding(
                    Binding,
                    ChatPayloadRepository,
                    FollowPayloadRepository,
                    SharePayloadRepository,
                    LikePayloadRepository,
                    RoomUserPayloadRepository,
                    GiftPayloadRepository,
                    MemberPayloadRepository
                ))
            {
                throw std::logic_error(
                    "Lifecycle binding references a missing payload"
                );
            }

            ValidatedEntries.push_back(
                FValidatedLifecycleEntry{
                    EmissionId,
                    Binding.FamilyKind,
                    Binding.PayloadHandle,
                    ExpectedState
                }
            );
        }

        // El enrutamiento por familia conserva el orden comunicado por el core.
        for (const FValidatedLifecycleEntry& Entry : ValidatedEntries)
        {
            if (!BindingRegistry.TransitionState(
                    Entry.EmissionId,
                    Entry.ExpectedState,
                    ETSExternalEmissionState::TerminalPendingHandling
                ))
            {
                throw std::logic_error(
                    "Lifecycle binding transition failed"
                );
            }

            if (!ErasePayloadForBinding(
                    Entry,
                    ChatPayloadRepository,
                    FollowPayloadRepository,
                    SharePayloadRepository,
                    LikePayloadRepository,
                    RoomUserPayloadRepository,
                    GiftPayloadRepository,
                    MemberPayloadRepository
                ))
            {
                throw std::logic_error(
                    "Lifecycle payload disappeared before cleanup"
                );
            }

            if (!BindingRegistry.Erase(Entry.EmissionId))
            {
                throw std::logic_error(
                    "Lifecycle binding disappeared before cleanup"
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

template <typename TPayload, typename TRepository>
FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitDecision(
    TTSFamilyDecision<TPayload> Decision,
    ETSEventFamilyKind ExpectedFamilyKind,
    ETSEventFlow ExpectedFlow,
    TRepository& Repository
)
{
    FTSPipelineAdmissionResult Result;

    if (!Decision.has_value())
    {
        return Result;
    }

    TTSAdmissionCandidate<TPayload> Candidate = std::move(*Decision);
    if (Candidate.FamilyKind != ExpectedFamilyKind ||
        Candidate.EnqueueRequest.Flow != ExpectedFlow)
    {
        throw std::logic_error(
            "Family decision does not match its coordinator route"
        );
    }

    const std::optional<FTSPayloadHandle> PayloadHandle =
        Repository.Insert(std::move(Candidate.Payload));

    if (!PayloadHandle.has_value())
    {
        Result.Status =
            ETSPipelineAdmissionStatus::RejectedPayloadIdentityExhausted;
        return Result;
    }

    TProvisionalPayloadGuard<TRepository> ProvisionalGuard(
        Repository,
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

    // El core ya comprometió la emisión; desde aquí no se simula rollback.
    ProvisionalGuard.Release();

    if (CoreResult.AdmittedEmission.EmissionId == 0)
    {
        throw std::logic_error(
            "Accepted emission has an invalid identity"
        );
    }

    if (CoreResult.AdmittedEmission.Flow != ExpectedFlow)
    {
        throw std::logic_error(
            "Accepted emission flow does not match its request"
        );
    }

    FTSEmissionBinding Binding;
    Binding.EmissionId = CoreResult.AdmittedEmission.EmissionId;
    Binding.FamilyKind = ExpectedFamilyKind;
    Binding.ExpectedFlow = ExpectedFlow;
    Binding.PayloadHandle = *PayloadHandle;
    Binding.ExternalState = ETSExternalEmissionState::Bound;

    if (!BindingRegistry.Insert(std::move(Binding)))
    {
        throw std::logic_error("Accepted emission could not be bound");
    }

    ProcessPendingLifecycleEvents(CoreResult.LifecycleEvents);
    CaptureCorePumpOutcome(CoreResult.AutoPumpOutcome);

    Result.Status = ETSPipelineAdmissionStatus::Accepted;
    Result.EnqueueResult = std::move(CoreResult);
    return Result;
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitChat(
    FTSChatInput Input
)
{
    return SubmitDecision(
        FTSChatFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Chat,
        ETSEventFlow::Chat,
        ChatPayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitFollow(
    FTSFollowInput Input
)
{
    return SubmitDecision(
        FTSFollowFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Follow,
        ETSEventFlow::Follow,
        FollowPayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitShare(
    FTSShareInput Input
)
{
    return SubmitDecision(
        FTSShareFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Share,
        ETSEventFlow::Share,
        SharePayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitLike(
    FTSLikeInput Input
)
{
    return SubmitDecision(
        FTSLikeFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Like,
        ETSEventFlow::Like,
        LikePayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitRoomUser(
    FTSRoomUserInput Input
)
{
    return SubmitDecision(
        FTSRoomUserFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::RoomUser,
        ETSEventFlow::RoomUser,
        RoomUserPayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitGift(
    FTSGiftInput Input
)
{
    return SubmitDecision(
        FTSGiftFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Gift,
        ETSEventFlow::Gift,
        GiftPayloadRepository
    );
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitMember(
    FTSMemberInput Input
)
{
    return SubmitDecision(
        FTSMemberFamily::Decide(std::move(Input)),
        ETSEventFamilyKind::Member,
        ETSEventFlow::MemberIdentity,
        MemberPayloadRepository
    );
}

FTSEmissionBinding FTSEventPipelineCoordinator::ValidateReadyBinding(
    const FTSEmissionEnvelope& ReadyEmission
) const
{
    if (ReadyEmission.EmissionId == 0)
    {
        throw std::logic_error("Pending ready has an invalid identity");
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
        throw std::logic_error("Pending ready has no binding");
    }

    if (Binding.EmissionId != ReadyEmission.EmissionId)
    {
        throw std::logic_error("Pending ready binding identity mismatch");
    }

    if (Binding.ExpectedFlow != ReadyEmission.Flow)
    {
        throw std::logic_error("Pending ready binding flow mismatch");
    }

    ValidateSupportedFamilyFlowPair(
        Binding.FamilyKind,
        Binding.ExpectedFlow
    );

    if (Binding.ExternalState != ETSExternalEmissionState::Bound)
    {
        throw std::logic_error("Pending ready binding is not Bound");
    }

    if (Binding.PayloadHandle.Value == 0)
    {
        throw std::logic_error(
            "Pending ready binding has an invalid payload handle"
        );
    }

    return Binding;
}

template <typename TPayload, typename TDispatch, typename TRepository>
TTSPipelineDispatchResult<TDispatch>
FTSEventPipelineCoordinator::BeginProcessing(
    ETSEventFamilyKind ExpectedFamilyKind,
    ETSEventFlow ExpectedFlow,
    TRepository& Repository
)
{
    TTSPipelineDispatchResult<TDispatch> Result;

    if (!PendingReadyEmission.has_value())
    {
        return Result;
    }

    const FTSEmissionEnvelope ReadyEmission = *PendingReadyEmission;
    const FTSEmissionBinding Binding = ValidateReadyBinding(ReadyEmission);

    // La inspección de otra familia no autoriza ni consume el ready compartido.
    if (Binding.FamilyKind != ExpectedFamilyKind)
    {
        return Result;
    }

    if (Binding.ExpectedFlow != ExpectedFlow)
    {
        throw std::logic_error(
            "Pending ready does not match the requested processing route"
        );
    }

    TPayload Payload;
    const bool bFoundPayload = Repository.Visit(
        Binding.PayloadHandle,
        [&](const TPayload& StoredPayload)
        {
            Payload = StoredPayload;
        }
    );

    if (!bFoundPayload)
    {
        throw std::logic_error("Pending ready binding has no payload");
    }

    TDispatch Dispatch{ReadyEmission, std::move(Payload)};
    Result.Status = ETSPipelineDispatchStatus::Dispatched;
    Result.Dispatch.emplace(std::move(Dispatch));

    // Toda operación potencialmente lanzable terminó antes del compromiso.
    if (!BindingRegistry.TransitionState(
            Binding.EmissionId,
            ETSExternalEmissionState::Bound,
            ETSExternalEmissionState::Processing
        ))
    {
        throw std::logic_error("Pending ready could not enter Processing");
    }

    PendingReadyEmission.reset();
    return Result;
}

FTSChatDispatchResult FTSEventPipelineCoordinator::BeginChatProcessing()
{
    return BeginProcessing<
        FTSChatPayload,
        FTSChatProcessingDispatch
    >(
        ETSEventFamilyKind::Chat,
        ETSEventFlow::Chat,
        ChatPayloadRepository
    );
}

FTSFollowDispatchResult FTSEventPipelineCoordinator::BeginFollowProcessing()
{
    return BeginProcessing<
        FTSFollowPayload,
        FTSFollowProcessingDispatch
    >(
        ETSEventFamilyKind::Follow,
        ETSEventFlow::Follow,
        FollowPayloadRepository
    );
}

FTSShareDispatchResult FTSEventPipelineCoordinator::BeginShareProcessing()
{
    return BeginProcessing<
        FTSSharePayload,
        FTSShareProcessingDispatch
    >(
        ETSEventFamilyKind::Share,
        ETSEventFlow::Share,
        SharePayloadRepository
    );
}

FTSLikeDispatchResult FTSEventPipelineCoordinator::BeginLikeProcessing()
{
    return BeginProcessing<
        FTSLikePayload,
        FTSLikeProcessingDispatch
    >(
        ETSEventFamilyKind::Like,
        ETSEventFlow::Like,
        LikePayloadRepository
    );
}

FTSRoomUserDispatchResult
FTSEventPipelineCoordinator::BeginRoomUserProcessing()
{
    return BeginProcessing<
        FTSRoomUserPayload,
        FTSRoomUserProcessingDispatch
    >(
        ETSEventFamilyKind::RoomUser,
        ETSEventFlow::RoomUser,
        RoomUserPayloadRepository
    );
}

FTSGiftDispatchResult FTSEventPipelineCoordinator::BeginGiftProcessing()
{
    return BeginProcessing<
        FTSGiftPayload,
        FTSGiftProcessingDispatch
    >(
        ETSEventFamilyKind::Gift,
        ETSEventFlow::Gift,
        GiftPayloadRepository
    );
}

FTSMemberDispatchResult FTSEventPipelineCoordinator::BeginMemberProcessing()
{
    return BeginProcessing<
        FTSMemberPayload,
        FTSMemberProcessingDispatch
    >(
        ETSEventFamilyKind::Member,
        ETSEventFlow::MemberIdentity,
        MemberPayloadRepository
    );
}

template <typename TRepository>
FTSProcessingCompletionResult FTSEventPipelineCoordinator::CompleteProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult,
    ETSEventFamilyKind ExpectedFamilyKind,
    ETSEventFlow ExpectedFlow,
    const TRepository& Repository
)
{
    FTSProcessingCompletionResult Result;
    Result.EmissionId = EmissionId;
    Result.ProcessingResult = ProcessingResult;

    if (EmissionId == 0)
    {
        throw std::logic_error(
            "Processing completion references an invalid identity"
        );
    }

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error(
            "Processing completion cannot coexist with a pending ready"
        );
    }

    switch (ProcessingResult)
    {
    case ETSProcessingResult::Succeeded:
    case ETSProcessingResult::Cancelled:
    case ETSProcessingResult::Failed:
        break;

    default:
        throw std::logic_error("Processing completion result is invalid");
    }

    // Todas las autoridades externas se validan antes de mutar el core.
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
        throw std::logic_error("Processing completion has no binding");
    }

    if (Binding.EmissionId != EmissionId)
    {
        throw std::logic_error(
            "Processing completion binding identity mismatch"
        );
    }

    if (Binding.FamilyKind != ExpectedFamilyKind)
    {
        throw std::logic_error(
            "Processing completion binding family mismatch"
        );
    }

    if (Binding.ExpectedFlow != ExpectedFlow)
    {
        throw std::logic_error(
            "Processing completion binding flow mismatch"
        );
    }

    ValidateSupportedFamilyFlowPair(
        Binding.FamilyKind,
        Binding.ExpectedFlow
    );

    if (Binding.ExternalState != ETSExternalEmissionState::Processing)
    {
        throw std::logic_error(
            "Processing completion binding is not Processing"
        );
    }

    if (Binding.PayloadHandle.Value == 0)
    {
        throw std::logic_error(
            "Processing completion binding has an invalid payload handle"
        );
    }

    const bool bFoundPayload = Repository.Visit(
        Binding.PayloadHandle,
        [](const auto&)
        {
        }
    );

    if (!bFoundPayload)
    {
        throw std::logic_error(
            "Processing completion binding has no payload"
        );
    }

    if (ProcessingResult == ETSProcessingResult::Succeeded)
    {
        FTSConfirmResult CoreResult = Core.Confirm(EmissionId);

        if (CoreResult.Status != ETSConfirmStatus::Confirmed)
        {
            throw std::logic_error(
                "Core rejected a valid processing confirmation"
            );
        }

        ProcessConfirmLifecycleEvents(
            EmissionId,
            CoreResult.LifecycleEvents
        );
        CaptureCorePumpOutcome(CoreResult.AutoPumpOutcome);
        Result.ConfirmResult.emplace(std::move(CoreResult));
        return Result;
    }

    // Failed también es terminal; el Pipeline no crea retry implícito.
    FTSCancelInFlightResult CoreResult = Core.CancelInFlight(EmissionId);

    if (CoreResult.Status != ETSCancelInFlightStatus::Cancelled)
    {
        throw std::logic_error(
            "Core rejected a valid processing cancellation"
        );
    }

    ProcessCancelLifecycleEvents(
        EmissionId,
        CoreResult.LifecycleEvents
    );
    Result.CancelResult.emplace(std::move(CoreResult));
    return Result;
}

FTSChatProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteChatProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Chat,
        ETSEventFlow::Chat,
        ChatPayloadRepository
    );
}

FTSFollowProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteFollowProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Follow,
        ETSEventFlow::Follow,
        FollowPayloadRepository
    );
}

FTSShareProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteShareProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Share,
        ETSEventFlow::Share,
        SharePayloadRepository
    );
}

FTSLikeProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteLikeProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Like,
        ETSEventFlow::Like,
        LikePayloadRepository
    );
}

FTSRoomUserProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteRoomUserProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::RoomUser,
        ETSEventFlow::RoomUser,
        RoomUserPayloadRepository
    );
}

FTSGiftProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteGiftProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Gift,
        ETSEventFlow::Gift,
        GiftPayloadRepository
    );
}

FTSMemberProcessingCompletionResult
FTSEventPipelineCoordinator::CompleteMemberProcessing(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return CompleteProcessing(
        EmissionId,
        ProcessingResult,
        ETSEventFamilyKind::Member,
        ETSEventFlow::MemberIdentity,
        MemberPayloadRepository
    );
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

std::optional<ETSEventFamilyKind>
FTSEventPipelineCoordinator::PeekPendingReadyFamilyKind() const
{
    if (!PendingReadyEmission.has_value())
    {
        return std::nullopt;
    }

    return ValidateReadyBinding(*PendingReadyEmission).FamilyKind;
}

std::size_t FTSEventPipelineCoordinator::GetBindingCount() const noexcept
{
    return BindingRegistry.Size();
}

std::size_t FTSEventPipelineCoordinator::GetChatPayloadCount() const noexcept
{
    return ChatPayloadRepository.Size();
}

std::size_t FTSEventPipelineCoordinator::GetFollowPayloadCount() const noexcept
{
    return FollowPayloadRepository.Size();
}

std::size_t FTSEventPipelineCoordinator::GetSharePayloadCount() const noexcept
{
    return SharePayloadRepository.Size();
}

std::size_t FTSEventPipelineCoordinator::GetLikePayloadCount() const noexcept
{
    return LikePayloadRepository.Size();
}

std::size_t
FTSEventPipelineCoordinator::GetRoomUserPayloadCount() const noexcept
{
    return RoomUserPayloadRepository.Size();
}

std::size_t FTSEventPipelineCoordinator::GetGiftPayloadCount() const noexcept
{
    return GiftPayloadRepository.Size();
}

std::size_t FTSEventPipelineCoordinator::GetMemberPayloadCount() const noexcept
{
    return MemberPayloadRepository.Size();
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
    (void)ValidateReadyBinding(ReadyEmission);

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error("A ready notification is already pending");
    }

    PendingReadyEmission = ReadyEmission;
}

void FTSEventPipelineCoordinator::ProcessPendingLifecycleEvents(
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        ELifecycleBatchKind::PendingOnly,
        0
    );
}

void FTSEventPipelineCoordinator::ProcessConfirmLifecycleEvents(
    FTSEmissionId EmissionId,
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        ELifecycleBatchKind::Confirm,
        EmissionId
    );
}

void FTSEventPipelineCoordinator::ProcessCancelLifecycleEvents(
    FTSEmissionId EmissionId,
    const FTSEmissionLifecycleEvents& LifecycleEvents
)
{
    ValidateAndApplyLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        LifecycleEvents,
        ELifecycleBatchKind::Cancel,
        EmissionId
    );
}
