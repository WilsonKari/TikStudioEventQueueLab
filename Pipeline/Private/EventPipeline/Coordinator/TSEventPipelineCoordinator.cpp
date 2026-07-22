#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Families/TSFollowFamily.h"
#include "EventPipeline/Families/TSShareFamily.h"
#include "EventPipeline/Families/TSLikeFamily.h"
#include "EventPipeline/Families/TSRoomUserFamily.h"
#include "EventPipeline/Families/TSGiftFamily.h"
#include "EventPipeline/Families/TSMemberFamily.h"

#include <algorithm>
#include <exception>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
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
        std::optional<FTSChatPendingBatchIndex::FPreparedEraseExact>
            ChatIndexErase;
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
        const FTSChatPendingBatchIndex& ChatPendingBatchIndex,
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

            std::optional<
                FTSChatPendingBatchIndex::FPreparedEraseExact
            > ChatIndexErase;
            if (Binding.FamilyKind == ETSEventFamilyKind::Chat)
            {
                std::string UserUniqueId;
                const bool bFoundChatPayload = ChatPayloadRepository.Visit(
                    Binding.PayloadHandle,
                    [&](const FTSChatPayload& Payload)
                    {
                        UserUniqueId = Payload.User.UniqueId;
                    }
                );
                if (!bFoundChatPayload || UserUniqueId.empty())
                {
                    throw std::logic_error(
                        "Chat lifecycle has no valid semantic payload"
                    );
                }

                if (ExpectedState == ETSExternalEmissionState::Bound)
                {
                    ChatIndexErase =
                        ChatPendingBatchIndex.PrepareEraseExact(
                            UserUniqueId,
                            EmissionId
                        );
                    if (!ChatIndexErase.has_value())
                    {
                        throw std::logic_error(
                            "Pending Chat lifecycle has no exact index entry"
                        );
                    }
                }
                else if (ChatPendingBatchIndex.ContainsExact(
                             UserUniqueId,
                             EmissionId
                         ))
                {
                    throw std::logic_error(
                        "Processing Chat remains mutable in the pending index"
                    );
                }
            }

            const bool bClearsReady = PendingReadyEmission.has_value() &&
                PendingReadyEmission->EmissionId == EmissionId;

            FValidatedLifecycleEntry Entry;
            Entry.EmissionId = EmissionId;
            Entry.FamilyKind = Binding.FamilyKind;
            Entry.PayloadHandle = Binding.PayloadHandle;
            Entry.ExpectedState = ExpectedState;
            Entry.bClearsReady = bClearsReady;
            Entry.ChatIndexErase = std::move(ChatIndexErase);
            Prepared.Entries.push_back(std::move(Entry));
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
        FTSChatPendingBatchIndex& ChatPendingBatchIndex,
        std::optional<FTSEmissionEnvelope>& PendingReadyEmission,
        FPreparedLifecycleBatch& Prepared
    ) noexcept
    {
        for (FValidatedLifecycleEntry& Entry : Prepared.Entries)
        {
            if (Entry.ChatIndexErase.has_value())
            {
                // Un lote deja de ser ampliable antes de limpiar payload y binding.
                ChatPendingBatchIndex.CommitEraseExact(*Entry.ChatIndexErase);
            }
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

namespace
{
    [[nodiscard]]
    FTSEventPipelineSettings ValidatePipelineSettings(
        FTSEventPipelineSettings Settings
    )
    {
        if (!FTSCommonUserPriorityPolicy::AreSettingsValid(
                Settings.CommonUserPriority
            ) ||
            !AreChatSemanticSettingsValid(Settings.Chat))
        {
            throw std::invalid_argument("Invalid Event Pipeline settings");
        }
        return Settings;
    }

    [[nodiscard]]
    std::shared_ptr<FTSNowProvider> MakeEffectiveNowProvider(
        FTSNowProvider NowProvider
    )
    {
        if (!NowProvider)
        {
            NowProvider = []()
            {
                return FTSEventQueueClock::now();
            };
        }
        return std::make_shared<FTSNowProvider>(std::move(NowProvider));
    }
}

FTSEventPipelineCoordinator::FTSEventPipelineCoordinator(
    FTSEventQueueSettings CoreSettings,
    FTSNowProvider NowProvider,
    FTSEventPipelineSettings InPipelineSettings
)
    : PipelineSettings(ValidatePipelineSettings(
          std::move(InPipelineSettings)
      ))
    , EffectiveNowProvider(MakeEffectiveNowProvider(
          std::move(NowProvider)
      ))
    , Core(
          std::move(CoreSettings),
          [Provider = EffectiveNowProvider]()
          {
              return (*Provider)();
          }
      )
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
    FPreparedCorePumpOutcome PreparedReady;

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
                ChatPendingBatchIndex,
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
        ChatPendingBatchIndex,
        PendingReadyEmission,
        PreparedLifecycle
    );
    CommitCorePumpOutcome(PreparedReady);
    ProvisionalGuard.Release();

    Result.Status = ETSPipelineAdmissionStatus::Accepted;
    Result.AffectedEmissionId = CoreResult.AdmittedEmission.EmissionId;
    Result.EnqueueResult = std::move(CoreResult);
    return Result;
}

FTSPipelineAdmissionResult FTSEventPipelineCoordinator::SubmitChat(
    FTSChatInput Input
)
{
    // ReceivedAt y la comparación de expiración comparten una sola captura del
    // proveedor efectivo en el owner thread.
    const FTSEventQueueTimePoint Now = (*EffectiveNowProvider)();
    FTSChatFamilyDecision Decision = FTSChatFamily::Decide(
        std::move(Input),
        Now,
        PipelineSettings
    );

    FTSPipelineAdmissionResult Result;
    switch (Decision.Status)
    {
    case ETSChatFamilyDecisionStatus::NoEmission:
        return Result;

    case ETSChatFamilyDecisionStatus::RejectedInvalidInput:
        Result.Status = ETSPipelineAdmissionStatus::RejectedInvalidInput;
        return Result;

    case ETSChatFamilyDecisionStatus::RejectedSemanticLimit:
        Result.Status = ETSPipelineAdmissionStatus::RejectedSemanticLimit;
        return Result;

    case ETSChatFamilyDecisionStatus::Candidate:
        break;

    default:
        throw std::logic_error("Chat family returned an invalid status");
    }

    if (!Decision.Candidate.has_value())
    {
        throw std::logic_error("Chat candidate status has no candidate");
    }

    TTSAdmissionCandidate<FTSChatPayload> Candidate =
        std::move(*Decision.Candidate);
    if (Candidate.FamilyKind != ETSEventFamilyKind::Chat ||
        Candidate.EnqueueRequest.Flow != ETSEventFlow::Chat ||
        Candidate.Payload.Messages.size() != 1)
    {
        throw std::logic_error("Chat candidate has an invalid route or shape");
    }

    const std::string UserUniqueId = Candidate.Payload.User.UniqueId;
    std::optional<FTSChatPendingBatchEntry> ExistingEntry;
    const bool bFoundExistingEntry = ChatPendingBatchIndex.VisitByUser(
        UserUniqueId,
        [&](const FTSChatPendingBatchEntry& Entry)
        {
            ExistingEntry = Entry;
        }
    );
    if (bFoundExistingEntry != ExistingEntry.has_value())
    {
        throw std::logic_error(
            "Pending Chat lookup returned an incoherent result"
        );
    }

    if (ExistingEntry.has_value() && Now < ExistingEntry->ExpiresAt)
    {
        FTSEmissionBinding Binding;
        const bool bFoundBinding = BindingRegistry.Visit(
            ExistingEntry->EmissionId,
            [&](const FTSEmissionBinding& StoredBinding)
            {
                Binding = StoredBinding;
            }
        );
        if (!bFoundBinding ||
            Binding.FamilyKind != ETSEventFamilyKind::Chat ||
            Binding.ExpectedFlow != ETSEventFlow::Chat ||
            Binding.ExternalState != ETSExternalEmissionState::Bound ||
            Binding.PayloadHandle.Value == 0 ||
            (PendingReadyEmission.has_value() &&
             PendingReadyEmission->EmissionId == ExistingEntry->EmissionId))
        {
            throw std::logic_error(
                "Pending Chat index does not match a mutable binding"
            );
        }

        FTSChatPayload ExpandedPayload;
        const bool bFoundPayload = ChatPayloadRepository.Visit(
            Binding.PayloadHandle,
            [&](const FTSChatPayload& StoredPayload)
            {
                ExpandedPayload = StoredPayload;
            }
        );
        if (!bFoundPayload || ExpandedPayload.User.UniqueId != UserUniqueId)
        {
            throw std::logic_error(
                "Pending Chat index does not match its payload"
            );
        }

        ExpandedPayload.User = std::move(Candidate.Payload.User);
        ExpandedPayload.Messages.push_back(
            std::move(Candidate.Payload.Messages.front())
        );
        if (!FTSChatFamily::IsWithinBatchLimits(
                ExpandedPayload,
                PipelineSettings.Chat
            ))
        {
            Result.Status =
                ETSPipelineAdmissionStatus::RejectedSemanticLimit;
            return Result;
        }

        std::optional<FTSChatPayloadRepository::FPreparedReplace>
            PreparedReplace = ChatPayloadRepository.PrepareReplace(
                Binding.PayloadHandle,
                std::move(ExpandedPayload)
            );
        if (!PreparedReplace.has_value())
        {
            throw std::logic_error(
                "Pending Chat payload could not prepare replacement"
            );
        }

        ChatPayloadRepository.CommitReplace(*PreparedReplace);
        Result.Status = ETSPipelineAdmissionStatus::Accumulated;
        Result.AffectedEmissionId = ExistingEntry->EmissionId;
        return Result;
    }

    // La prioridad común se congela sólo cuando nace un lote; acumulaciones no la
    // recalculan ni multiplican por el número de mensajes.
    const FTSCommonUserPriorityBreakdown Priority =
        FTSCommonUserPriorityPolicy::Evaluate(
            Candidate.Payload.User,
            PipelineSettings.CommonUserPriority
        );
    Candidate.Payload.CommonPriorityAdjustment = Priority.TotalAdjustment;
    Candidate.EnqueueRequest.PriorityAdjustment = Priority.TotalAdjustment;

    const std::optional<FTSPayloadHandle> PayloadHandle =
        ChatPayloadRepository.Insert(std::move(Candidate.Payload));
    if (!PayloadHandle.has_value())
    {
        Result.Status =
            ETSPipelineAdmissionStatus::RejectedPayloadIdentityExhausted;
        return Result;
    }

    TProvisionalPayloadGuard<FTSChatPayloadRepository> ProvisionalGuard(
        ChatPayloadRepository,
        *PayloadHandle
    );
    std::optional<FTSEmissionBindingRegistry::FPreparedInsert>
        PreparedBindingInsert;
    std::optional<FTSChatPendingBatchIndex::FPreparedInsert>
        PreparedIndexInsert;
    FPreparedLifecycleBatch PreparedLifecycle;
    FPreparedCorePumpOutcome PreparedReady;

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
                        "Rejected Chat admission prepared Core mutations"
                    );
                }
                return;
            }

            const FTSEmissionEnvelope& Admitted =
                PreparedCoreResult.AdmittedEmission;
            if (Admitted.EmissionId == 0 ||
                Admitted.Flow != ETSEventFlow::Chat)
            {
                throw std::logic_error(
                    "Accepted Chat emission has invalid identity or flow"
                );
            }

            FTSEmissionBinding Binding;
            Binding.EmissionId = Admitted.EmissionId;
            Binding.FamilyKind = ETSEventFamilyKind::Chat;
            Binding.ExpectedFlow = ETSEventFlow::Chat;
            Binding.PayloadHandle = *PayloadHandle;
            Binding.ExternalState = ETSExternalEmissionState::Bound;

            auto BindingInsert = BindingRegistry.PrepareInsert(Binding);
            if (!BindingInsert.has_value())
            {
                throw std::logic_error(
                    "Accepted Chat could not prepare its binding"
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
                ChatPendingBatchIndex,
                PendingReadyEmission,
                PreparedCoreResult.LifecycleEvents,
                ELifecycleBatchKind::PendingOnly,
                0
            );
            PreparedReady = PrepareCorePumpOutcome(
                PreparedCoreResult.AutoPumpOutcome,
                &Binding
            );

            const bool bAdmittedIsReady =
                PreparedCoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                PreparedCoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    Admitted.EmissionId;
            if (!bAdmittedIsReady)
            {
                const FTSChatPendingBatchEntry NewEntry{
                    Admitted.EmissionId,
                    Admitted.ExpiresAt
                };
                if (ExistingEntry.has_value())
                {
                    const bool bExpiredLifecycleFound =
                        std::any_of(
                            PreparedCoreResult.LifecycleEvents.begin(),
                            PreparedCoreResult.LifecycleEvents.end(),
                            [&](const FTSEmissionLifecycleEvent& Event)
                            {
                                return Event.Envelope.EmissionId ==
                                    ExistingEntry->EmissionId;
                            }
                        );
                    if (!bExpiredLifecycleFound)
                    {
                        throw std::logic_error(
                            "Expired Chat predecessor was not terminalized"
                        );
                    }
                    PreparedIndexInsert =
                        ChatPendingBatchIndex.PrepareInsertAfterExactErase(
                            UserUniqueId,
                            ExistingEntry->EmissionId,
                            NewEntry
                        );
                }
                else
                {
                    PreparedIndexInsert = ChatPendingBatchIndex.PrepareInsert(
                        UserUniqueId,
                        NewEntry
                    );
                }

                if (!PreparedIndexInsert.has_value())
                {
                    throw std::logic_error(
                        "Accepted pending Chat could not prepare its index"
                    );
                }
            }

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
        ChatPendingBatchIndex,
        PendingReadyEmission,
        PreparedLifecycle
    );
    CommitCorePumpOutcome(PreparedReady);
    if (PreparedIndexInsert.has_value())
    {
        ChatPendingBatchIndex.CommitInsert(*PreparedIndexInsert);
    }
    ProvisionalGuard.Release();

    Result.Status = ETSPipelineAdmissionStatus::Accepted;
    Result.AffectedEmissionId = CoreResult.AdmittedEmission.EmissionId;
    Result.EnqueueResult = std::move(CoreResult);
    return Result;
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

    if (Binding.FamilyKind == ETSEventFamilyKind::Chat)
    {
        std::string UserUniqueId;
        const bool bFoundChatPayload = ChatPayloadRepository.Visit(
            Binding.PayloadHandle,
            [&](const FTSChatPayload& Payload)
            {
                UserUniqueId = Payload.User.UniqueId;
            }
        );
        if (!bFoundChatPayload || UserUniqueId.empty() ||
            ChatPendingBatchIndex.ContainsExact(
                UserUniqueId,
                Binding.EmissionId
            ))
        {
            throw std::logic_error(
                "Ready Chat remains mutable in the pending index"
            );
        }
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
    FPreparedCorePumpOutcome PreparedReady;

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
                    ChatPendingBatchIndex,
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
            ChatPendingBatchIndex,
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
                ChatPendingBatchIndex,
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
        ChatPendingBatchIndex,
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
    FPreparedCorePumpOutcome PreparedReady;
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
                ChatPendingBatchIndex,
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
        ChatPendingBatchIndex,
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
                    ChatPendingBatchIndex,
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
        ChatPendingBatchIndex,
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

std::size_t
FTSEventPipelineCoordinator::GetPendingChatBatchCount() const noexcept
{
    return ChatPendingBatchIndex.Size();
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

            if (Binding.FamilyKind == ETSEventFamilyKind::Chat)
            {
                std::string UserUniqueId;
                const bool bFoundPayload = ChatPayloadRepository.Visit(
                    Binding.PayloadHandle,
                    [&](const FTSChatPayload& Payload)
                    {
                        UserUniqueId = Payload.User.UniqueId;
                    }
                );
                if (!bFoundPayload || UserUniqueId.empty())
                {
                    throw std::logic_error(
                        "Live Chat binding has no valid semantic payload"
                    );
                }

                const bool bIndexed = ChatPendingBatchIndex.ContainsExact(
                    UserUniqueId,
                    Binding.EmissionId
                );
                const bool bReady = PendingReadyEmission.has_value() &&
                    PendingReadyEmission->EmissionId == Binding.EmissionId;
                if (Binding.ExternalState == ETSExternalEmissionState::Bound &&
                    bIndexed == bReady)
                {
                    throw std::logic_error(
                        "Bound Chat index does not match ready state"
                    );
                }
                if (Binding.ExternalState ==
                        ETSExternalEmissionState::Processing &&
                    bIndexed)
                {
                    throw std::logic_error(
                        "Processing Chat remains in the pending index"
                    );
                }
            }

            if (Binding.ExternalState ==
                ETSExternalEmissionState::Processing)
            {
                ++ProcessingCount;
            }
        }
    );

    std::size_t IndexedChatCount = 0;
    std::unordered_set<FTSEmissionId> IndexedEmissionIds;
    IndexedEmissionIds.reserve(ChatPendingBatchIndex.Size());
    ChatPendingBatchIndex.VisitAll(
        [&](const std::string& UserUniqueId,
            const FTSChatPendingBatchEntry& Entry)
        {
            ++IndexedChatCount;
            if (!IndexedEmissionIds.insert(Entry.EmissionId).second)
            {
                throw std::logic_error(
                    "Two Chat users reference the same pending emission"
                );
            }
            FTSEmissionBinding Binding;
            const bool bFoundBinding = BindingRegistry.Visit(
                Entry.EmissionId,
                [&](const FTSEmissionBinding& StoredBinding)
                {
                    Binding = StoredBinding;
                }
            );
            if (!bFoundBinding ||
                Binding.FamilyKind != ETSEventFamilyKind::Chat ||
                Binding.ExpectedFlow != ETSEventFlow::Chat ||
                Binding.ExternalState != ETSExternalEmissionState::Bound ||
                (PendingReadyEmission.has_value() &&
                 PendingReadyEmission->EmissionId == Entry.EmissionId))
            {
                throw std::logic_error(
                    "Chat pending index references a non-mutable binding"
                );
            }

            bool bPayloadMatches = false;
            const bool bFoundPayload = ChatPayloadRepository.Visit(
                Binding.PayloadHandle,
                [&](const FTSChatPayload& Payload)
                {
                    bPayloadMatches = Payload.User.UniqueId == UserUniqueId;
                }
            );
            if (!bFoundPayload || !bPayloadMatches)
            {
                throw std::logic_error(
                    "Chat pending index key does not match its payload"
                );
            }
        }
    );
    if (IndexedChatCount != ChatPendingBatchIndex.Size())
    {
        throw std::logic_error("Chat pending index changed during validation");
    }

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

FTSEventPipelineCoordinator::FPreparedCorePumpOutcome
FTSEventPipelineCoordinator::PrepareCorePumpOutcome(
    const FTSPumpOutcome& PumpOutcome,
    const FTSEmissionBinding* PreparedBinding
) const
{
    FPreparedCorePumpOutcome Prepared;
    if (PumpOutcome.Status != ETSPumpStatus::EmissionReady)
    {
        return Prepared;
    }

    if (PendingReadyEmission.has_value())
    {
        throw std::logic_error("A ready notification is already pending");
    }

    const FTSEmissionEnvelope& ReadyEmission = PumpOutcome.ReadyEmission;
    FTSEmissionBinding ReadyBinding;
    const bool bUsesPreparedBinding = PreparedBinding != nullptr &&
        PreparedBinding->EmissionId == ReadyEmission.EmissionId;
    if (bUsesPreparedBinding)
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
        ReadyBinding = *PreparedBinding;
    }
    else
    {
        ReadyBinding = ValidateReadyBinding(ReadyEmission);
    }

    if (ReadyBinding.FamilyKind == ETSEventFamilyKind::Chat &&
        !bUsesPreparedBinding)
    {
        std::string UserUniqueId;
        const bool bFoundPayload = ChatPayloadRepository.Visit(
            ReadyBinding.PayloadHandle,
            [&](const FTSChatPayload& Payload)
            {
                UserUniqueId = Payload.User.UniqueId;
            }
        );
        if (!bFoundPayload || UserUniqueId.empty())
        {
            throw std::logic_error(
                "Ready Chat has no valid semantic payload"
            );
        }

        Prepared.ChatIndexErase = ChatPendingBatchIndex.PrepareEraseExact(
            UserUniqueId,
            ReadyEmission.EmissionId
        );
        if (!Prepared.ChatIndexErase.has_value())
        {
            throw std::logic_error(
                "Ready Chat has no exact pending index entry"
            );
        }
    }

    Prepared.ReadyEmission = ReadyEmission;
    return Prepared;
}

void FTSEventPipelineCoordinator::CommitCorePumpOutcome(
    FPreparedCorePumpOutcome& PreparedOutcome
) noexcept
{
    if (!PreparedOutcome.ReadyEmission.has_value())
    {
        return;
    }

    if (PendingReadyEmission.has_value())
    {
        std::terminate();
    }

    if (PreparedOutcome.ChatIndexErase.has_value())
    {
        // La selección autoritativa del Core cierra la ventana de acumulación.
        ChatPendingBatchIndex.CommitEraseExact(
            *PreparedOutcome.ChatIndexErase
        );
    }

    // Ready sólo notifica el InFlight ya comprometido; no introduce otra autoridad.
    PendingReadyEmission.emplace(
        std::move(*PreparedOutcome.ReadyEmission)
    );
    PreparedOutcome.ReadyEmission.reset();
}
