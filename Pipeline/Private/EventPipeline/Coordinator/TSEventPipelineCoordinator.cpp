#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Families/TSFollowFamily.h"
#include "EventPipeline/Families/TSShareFamily.h"
#include "EventPipeline/Families/TSLikeFamily.h"
#include "EventPipeline/Families/TSRoomUserFamily.h"
#include "EventPipeline/Families/TSGiftFamily.h"
#include "EventPipeline/Families/TSMemberFamily.h"

#include <exception>
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
static_assert(
    std::is_nothrow_move_constructible_v<FTSEmissionEnvelope>
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
        bool bClearsReady = false;
    };

    struct FPreparedLifecycleBatch
    {
        std::vector<FValidatedLifecycleEntry> Entries;
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
    void CommitErasePayloadForBinding(
        const FValidatedLifecycleEntry& Entry,
        FTSChatPayloadRepository& ChatPayloadRepository,
        FTSFollowPayloadRepository& FollowPayloadRepository,
        FTSSharePayloadRepository& SharePayloadRepository,
        FTSLikePayloadRepository& LikePayloadRepository,
        FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        FTSGiftPayloadRepository& GiftPayloadRepository,
        FTSMemberPayloadRepository& MemberPayloadRepository
    ) noexcept
    {
        switch (Entry.FamilyKind)
        {
        case ETSEventFamilyKind::Chat:
            ChatPayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::Follow:
            FollowPayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::Share:
            SharePayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::Like:
            LikePayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::RoomUser:
            RoomUserPayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::Gift:
            GiftPayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        case ETSEventFamilyKind::Member:
            MemberPayloadRepository.CommitErase(Entry.PayloadHandle);
            return;

        default:
            std::terminate();
        }
    }

    [[nodiscard]]
    FPreparedLifecycleBatch PrepareLifecycleBatch(
        const FTSEmissionBindingRegistry& BindingRegistry,
        const FTSChatPayloadRepository& ChatPayloadRepository,
        const FTSFollowPayloadRepository& FollowPayloadRepository,
        const FTSSharePayloadRepository& SharePayloadRepository,
        const FTSLikePayloadRepository& LikePayloadRepository,
        const FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        const FTSGiftPayloadRepository& GiftPayloadRepository,
        const FTSMemberPayloadRepository& MemberPayloadRepository,
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

        // La tanda completa valida binding, payload, estado y pareja familia/flujo;
        // el ready se trata sólo como una notificación que no puede quedar stale.
        FPreparedLifecycleBatch Prepared;
        Prepared.Entries.reserve(LifecycleEvents.size());

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

            for (const FValidatedLifecycleEntry& Entry : Prepared.Entries)
            {
                if (Entry.EmissionId == EmissionId)
                {
                    throw std::logic_error(
                        "Lifecycle contains a duplicate identity"
                    );
                }
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

            const bool bClearsReady = PendingReadyEmission.has_value() &&
                PendingReadyEmission->EmissionId == EmissionId;

            Prepared.Entries.push_back(
                FValidatedLifecycleEntry{
                    EmissionId,
                    Binding.FamilyKind,
                    Binding.PayloadHandle,
                    ExpectedState,
                    bClearsReady
                }
            );
        }

        return Prepared;
    }

    // Toda asignación y validación ya ocurrió. Esta fase sincroniza y limpia las
    // autoridades externas en el orden autoritativo del Core sin poder lanzar.
    void CommitLifecycleBatch(
        FTSEmissionBindingRegistry& BindingRegistry,
        FTSChatPayloadRepository& ChatPayloadRepository,
        FTSFollowPayloadRepository& FollowPayloadRepository,
        FTSSharePayloadRepository& SharePayloadRepository,
        FTSLikePayloadRepository& LikePayloadRepository,
        FTSRoomUserPayloadRepository& RoomUserPayloadRepository,
        FTSGiftPayloadRepository& GiftPayloadRepository,
        FTSMemberPayloadRepository& MemberPayloadRepository,
        std::optional<FTSEmissionEnvelope>& PendingReadyEmission,
        const FPreparedLifecycleBatch& Prepared
    ) noexcept
    {
        for (const FValidatedLifecycleEntry& Entry : Prepared.Entries)
        {
            BindingRegistry.CommitTransitionState(
                Entry.EmissionId,
                Entry.ExpectedState,
                ETSExternalEmissionState::TerminalPendingHandling
            );
            CommitErasePayloadForBinding(
                Entry,
                ChatPayloadRepository,
                FollowPayloadRepository,
                SharePayloadRepository,
                LikePayloadRepository,
                RoomUserPayloadRepository,
                GiftPayloadRepository,
                MemberPayloadRepository
            );
            BindingRegistry.CommitErase(Entry.EmissionId);

            if (Entry.bClearsReady)
            {
                PendingReadyEmission.reset();
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

FTSUpdateFlowSettingsResult
FTSEventPipelineCoordinator::UpdateFlowSettings(
    ETSEventFlow Flow,
    const FTSFlowQueueSettings& NewSettings
)
{
    return Core.UpdateFlowSettings(Flow, NewSettings);
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
        Candidate.EnqueueRequest.Flow != ExpectedFlow ||
        !IsSupportedFamilyFlowPair(ExpectedFamilyKind, ExpectedFlow))
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

    // El repositorio posee provisionalmente el payload hasta que una admisión
    // aceptada permita vincular el handle con la identidad global del Core.
    TProvisionalPayloadGuard<TRepository> ProvisionalGuard(
        Repository,
        *PayloadHandle
    );

    std::optional<FTSEmissionBindingRegistry::FPreparedInsert>
        PreparedBindingInsert;
    FPreparedLifecycleBatch PreparedLifecycle;
    std::optional<FTSEmissionEnvelope> PreparedReady;

    FTSEnqueueResult CoreResult = Core.EnqueuePrepared(
        Candidate.EnqueueRequest,
        [&](const FTSEnqueueResult& PreparedCoreResult)
        {
            if (!IsAcceptedStatus(PreparedCoreResult.Status))
            {
                if (!PreparedCoreResult.LifecycleEvents.empty() ||
                    PreparedCoreResult.AutoPumpOutcome.Status !=
                        ETSPumpStatus::NotRequested)
                {
                    throw std::logic_error(
                        "Rejected admission prepared Core mutations"
                    );
                }
                return;
            }

            if (PreparedCoreResult.AdmittedEmission.EmissionId == 0)
            {
                throw std::logic_error(
                    "Accepted emission has an invalid identity"
                );
            }

            if (PreparedCoreResult.AdmittedEmission.Flow != ExpectedFlow)
            {
                throw std::logic_error(
                    "Accepted emission flow does not match its request"
                );
            }

            FTSEmissionBinding Binding;
            Binding.EmissionId =
                PreparedCoreResult.AdmittedEmission.EmissionId;
            Binding.FamilyKind = ExpectedFamilyKind;
            Binding.ExpectedFlow = ExpectedFlow;
            Binding.PayloadHandle = *PayloadHandle;
            Binding.ExternalState = ETSExternalEmissionState::Bound;

            std::optional<FTSEmissionBindingRegistry::FPreparedInsert>
                BindingInsert = BindingRegistry.PrepareInsert(Binding);
            if (!BindingInsert.has_value())
            {
                throw std::logic_error(
                    "Accepted emission could not prepare its binding"
                );
            }

            PreparedLifecycle = PrepareLifecycleBatch(
                BindingRegistry,
                ChatPayloadRepository,
                FollowPayloadRepository,
                SharePayloadRepository,
                LikePayloadRepository,
                RoomUserPayloadRepository,
                GiftPayloadRepository,
                MemberPayloadRepository,
                PendingReadyEmission,
                PreparedCoreResult.LifecycleEvents,
                ELifecycleBatchKind::PendingOnly,
                0
            );
            PreparedReady = PrepareCorePumpOutcome(
                PreparedCoreResult.AutoPumpOutcome,
                &Binding
            );
            PreparedBindingInsert.emplace(std::move(*BindingInsert));
        }
    );

    if (!IsAcceptedStatus(CoreResult.Status))
    {
        ProvisionalGuard.RollbackNow();

        Result.Status = ETSPipelineAdmissionStatus::RejectedByCore;
        Result.EnqueueResult = std::move(CoreResult);
        return Result;
    }

    // El callback ya reservó y validó todo. Desde el commit del Core hasta terminar
    // binding, lifecycle y ready sólo se ejecutan operaciones noexcept.
    BindingRegistry.CommitInsert(*PreparedBindingInsert);
    CommitLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        PreparedLifecycle
    );
    CommitCorePumpOutcome(PreparedReady);
    ProvisionalGuard.Release();

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
        ETSEventFlow::Member,
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
        throw std::logic_error("Pending ready binding has no payload");
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

    // El ready sólo notifica qué binding autoritativo puede despacharse; inspeccionar
    // otra familia no lo autoriza ni lo consume.
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

    ValidateSupportedFamilyFlowPair(ExpectedFamilyKind, ExpectedFlow);

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

    // Binding, payload y pareja familia/flujo se validan y el dispatch se construye
    // antes de comprometer Bound -> Processing y consumir la notificación ready.
    BindingRegistry.CommitTransitionState(
        Binding.EmissionId,
        ETSExternalEmissionState::Bound,
        ETSExternalEmissionState::Processing
    );
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
        ETSEventFlow::Member,
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

    // Binding, payload, estado y pareja familia/flujo se validan antes de mutar el
    // Core; tras su commit terminal, lifecycle sincroniza la limpieza externa.
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
        throw FTSRejectedProcessingCompletionError(
            "Processing completion has no binding"
        );
    }

    if (Binding.EmissionId != EmissionId)
    {
        throw std::logic_error(
            "Processing completion binding identity mismatch"
        );
    }

    ValidateSupportedFamilyFlowPair(
        Binding.FamilyKind,
        Binding.ExpectedFlow
    );

    if (Binding.PayloadHandle.Value == 0)
    {
        throw std::logic_error(
            "Processing completion binding has an invalid payload handle"
        );
    }

    // Sólo estas discrepancias describen un comando externo definitivamente
    // rechazado; las invariantes internas conservan std::logic_error para retry.
    if (Binding.FamilyKind != ExpectedFamilyKind)
    {
        throw FTSRejectedProcessingCompletionError(
            "Processing completion binding family mismatch"
        );
    }

    if (Binding.ExpectedFlow != ExpectedFlow)
    {
        throw FTSRejectedProcessingCompletionError(
            "Processing completion binding flow mismatch"
        );
    }

    if (Binding.ExternalState != ETSExternalEmissionState::Processing)
    {
        throw FTSRejectedProcessingCompletionError(
            "Processing completion binding is not Processing"
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

    FPreparedLifecycleBatch PreparedLifecycle;
    std::optional<FTSEmissionEnvelope> PreparedReady;

    if (ProcessingResult == ETSProcessingResult::Succeeded)
    {
        FTSConfirmResult CoreResult = Core.ConfirmPrepared(
            EmissionId,
            [&](const FTSConfirmResult& PreparedCoreResult)
            {
                if (PreparedCoreResult.Status != ETSConfirmStatus::Confirmed)
                {
                    throw std::logic_error(
                        "Core rejected a valid processing confirmation"
                    );
                }

                PreparedLifecycle = PrepareLifecycleBatch(
                    BindingRegistry,
                    ChatPayloadRepository,
                    FollowPayloadRepository,
                    SharePayloadRepository,
                    LikePayloadRepository,
                    RoomUserPayloadRepository,
                    GiftPayloadRepository,
                    MemberPayloadRepository,
                    PendingReadyEmission,
                    PreparedCoreResult.LifecycleEvents,
                    ELifecycleBatchKind::Confirm,
                    EmissionId
                );
                PreparedReady = PrepareCorePumpOutcome(
                    PreparedCoreResult.AutoPumpOutcome
                );
            }
        );

        CommitLifecycleBatch(
            BindingRegistry,
            ChatPayloadRepository,
            FollowPayloadRepository,
            SharePayloadRepository,
            LikePayloadRepository,
            RoomUserPayloadRepository,
            GiftPayloadRepository,
            MemberPayloadRepository,
            PendingReadyEmission,
            PreparedLifecycle
        );
        CommitCorePumpOutcome(PreparedReady);
        Result.ConfirmResult.emplace(std::move(CoreResult));
        return Result;
    }

    // Failed también es terminal; el Pipeline no crea retry implícito.
    FTSCancelInFlightResult CoreResult = Core.CancelInFlightPrepared(
        EmissionId,
        [&](const FTSCancelInFlightResult& PreparedCoreResult)
        {
            if (PreparedCoreResult.Status !=
                ETSCancelInFlightStatus::Cancelled)
            {
                throw std::logic_error(
                    "Core rejected a valid processing cancellation"
                );
            }

            PreparedLifecycle = PrepareLifecycleBatch(
                BindingRegistry,
                ChatPayloadRepository,
                FollowPayloadRepository,
                SharePayloadRepository,
                LikePayloadRepository,
                RoomUserPayloadRepository,
                GiftPayloadRepository,
                MemberPayloadRepository,
                PendingReadyEmission,
                PreparedCoreResult.LifecycleEvents,
                ELifecycleBatchKind::Cancel,
                EmissionId
            );
        }
    );

    CommitLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        PreparedLifecycle
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
        ETSEventFlow::Member,
        MemberPayloadRepository
    );
}

FTSPumpResult FTSEventPipelineCoordinator::Pump()
{
    FPreparedLifecycleBatch PreparedLifecycle;
    std::optional<FTSEmissionEnvelope> PreparedReady;
    FTSPumpResult Result = Core.PumpPrepared(
        [&](const FTSPumpResult& PreparedCoreResult)
        {
            PreparedLifecycle = PrepareLifecycleBatch(
                BindingRegistry,
                ChatPayloadRepository,
                FollowPayloadRepository,
                SharePayloadRepository,
                LikePayloadRepository,
                RoomUserPayloadRepository,
                GiftPayloadRepository,
                MemberPayloadRepository,
                PendingReadyEmission,
                PreparedCoreResult.LifecycleEvents,
                ELifecycleBatchKind::PendingOnly,
                0
            );
            PreparedReady = PrepareCorePumpOutcome(
                PreparedCoreResult.Outcome
            );
        }
    );

    CommitLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        PreparedLifecycle
    );
    CommitCorePumpOutcome(PreparedReady);
    return Result;
}

FTSProcessDueExpirationsResult
FTSEventPipelineCoordinator::ProcessDueExpirations()
{
    FPreparedLifecycleBatch PreparedLifecycle;
    FTSProcessDueExpirationsResult Result =
        Core.ProcessDueExpirationsPrepared(
            [&](const FTSProcessDueExpirationsResult& PreparedCoreResult)
            {
                PreparedLifecycle = PrepareLifecycleBatch(
                    BindingRegistry,
                    ChatPayloadRepository,
                    FollowPayloadRepository,
                    SharePayloadRepository,
                    LikePayloadRepository,
                    RoomUserPayloadRepository,
                    GiftPayloadRepository,
                    MemberPayloadRepository,
                    PendingReadyEmission,
                    PreparedCoreResult.LifecycleEvents,
                    ELifecycleBatchKind::PendingOnly,
                    0
                );
            }
        );

    CommitLifecycleBatch(
        BindingRegistry,
        ChatPayloadRepository,
        FollowPayloadRepository,
        SharePayloadRepository,
        LikePayloadRepository,
        RoomUserPayloadRepository,
        GiftPayloadRepository,
        MemberPayloadRepository,
        PendingReadyEmission,
        PreparedLifecycle
    );
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

void FTSEventPipelineCoordinator::ValidateInternalConsistency() const
{
    const std::size_t PayloadCount =
        ChatPayloadRepository.Size() +
        FollowPayloadRepository.Size() +
        SharePayloadRepository.Size() +
        LikePayloadRepository.Size() +
        RoomUserPayloadRepository.Size() +
        GiftPayloadRepository.Size() +
        MemberPayloadRepository.Size();
    if (PayloadCount != BindingRegistry.Size())
    {
        throw std::logic_error(
            "Pipeline payload and binding authorities have different sizes"
        );
    }

    std::size_t ProcessingCount = 0;
    BindingRegistry.VisitAll(
        [&](const FTSEmissionBinding& Binding)
        {
            if (Binding.EmissionId == 0 ||
                Binding.PayloadHandle.Value == 0 ||
                !IsSupportedFamilyFlowPair(
                    Binding.FamilyKind,
                    Binding.ExpectedFlow
                ) ||
                Binding.ExternalState ==
                    ETSExternalEmissionState::TerminalPendingHandling)
            {
                throw std::logic_error(
                    "Pipeline contains an invalid live binding"
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
                    "Pipeline binding references a missing payload"
                );
            }

            if (Binding.ExternalState ==
                ETSExternalEmissionState::Processing)
            {
                ++ProcessingCount;
            }
        }
    );

    if (ProcessingCount > 1)
    {
        throw std::logic_error(
            "Pipeline contains more than one Processing binding"
        );
    }

    if (PendingReadyEmission.has_value())
    {
        (void)ValidateReadyBinding(*PendingReadyEmission);
        if (ProcessingCount != 0)
        {
            throw std::logic_error(
                "Pipeline ready and Processing states overlap"
            );
        }
    }
}

std::optional<FTSEmissionEnvelope>
FTSEventPipelineCoordinator::PrepareCorePumpOutcome(
    const FTSPumpOutcome& PumpOutcome,
    const FTSEmissionBinding* PreparedBinding
) const
{
    if (PumpOutcome.Status != ETSPumpStatus::EmissionReady)
    {
        return std::nullopt;
    }

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error("A ready notification is already pending");
    }

    const FTSEmissionEnvelope& ReadyEmission = PumpOutcome.ReadyEmission;
    if (PreparedBinding != nullptr &&
        PreparedBinding->EmissionId == ReadyEmission.EmissionId)
    {
        if (PreparedBinding->ExpectedFlow != ReadyEmission.Flow ||
            PreparedBinding->ExternalState !=
                ETSExternalEmissionState::Bound ||
            !IsSupportedFamilyFlowPair(
                PreparedBinding->FamilyKind,
                PreparedBinding->ExpectedFlow
            ) ||
            !HasPayloadForBinding(
                *PreparedBinding,
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
                "Prepared ready does not match its admission binding"
            );
        }
    }
    else
    {
        (void)ValidateReadyBinding(ReadyEmission);
    }

    return ReadyEmission;
}

void FTSEventPipelineCoordinator::CommitCorePumpOutcome(
    std::optional<FTSEmissionEnvelope>& PreparedReady
) noexcept
{
    if (!PreparedReady.has_value())
    {
        return;
    }

    if (PendingReadyEmission.has_value())
    {
        std::terminate();
    }

    // Ready sólo notifica el InFlight ya comprometido; no introduce otra autoridad.
    PendingReadyEmission.emplace(std::move(*PreparedReady));
    PreparedReady.reset();
}
