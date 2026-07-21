#pragma once

#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Processing/TSChatProcessingCompletion.h"
#include "EventPipeline/Processing/TSChatProcessingDispatch.h"
#include "EventPipeline/Processing/TSFollowProcessingCompletion.h"
#include "EventPipeline/Processing/TSFollowProcessingDispatch.h"
#include "EventPipeline/Processing/TSShareProcessingCompletion.h"
#include "EventPipeline/Processing/TSShareProcessingDispatch.h"
#include "EventPipeline/Processing/TSLikeProcessingCompletion.h"
#include "EventPipeline/Processing/TSLikeProcessingDispatch.h"
#include "EventPipeline/Processing/TSRoomUserProcessingCompletion.h"
#include "EventPipeline/Processing/TSRoomUserProcessingDispatch.h"
#include "EventPipeline/Processing/TSGiftProcessingCompletion.h"
#include "EventPipeline/Processing/TSGiftProcessingDispatch.h"
#include "EventPipeline/Processing/TSMemberProcessingCompletion.h"
#include "EventPipeline/Processing/TSMemberProcessingDispatch.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"
#include "EventPipeline/Repositories/TSFollowPayloadRepository.h"
#include "EventPipeline/Repositories/TSSharePayloadRepository.h"
#include "EventPipeline/Repositories/TSLikePayloadRepository.h"
#include "EventPipeline/Repositories/TSRoomUserPayloadRepository.h"
#include "EventPipeline/Repositories/TSGiftPayloadRepository.h"
#include "EventPipeline/Repositories/TSMemberPayloadRepository.h"
#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

enum class ETSPipelineAdmissionStatus : std::uint8_t
{
    NoEmission,
    RejectedPayloadIdentityExhausted,
    RejectedByCore,
    Accepted
};

struct FTSPipelineAdmissionResult
{
    ETSPipelineAdmissionStatus Status =
        ETSPipelineAdmissionStatus::NoEmission;

    // Sólo contiene valor cuando el coordinador llegó a llamar al core.
    std::optional<FTSEnqueueResult> EnqueueResult;
};

enum class ETSPipelineDispatchStatus : std::uint8_t
{
    NoEmissionReady,
    Dispatched
};

template <typename TDispatch>
struct TTSPipelineDispatchResult
{
    ETSPipelineDispatchStatus Status =
        ETSPipelineDispatchStatus::NoEmissionReady;

    std::optional<TDispatch> Dispatch;
};

using FTSChatDispatchResult =
    TTSPipelineDispatchResult<FTSChatProcessingDispatch>;

using FTSFollowDispatchResult =
    TTSPipelineDispatchResult<FTSFollowProcessingDispatch>;

using FTSShareDispatchResult =
    TTSPipelineDispatchResult<FTSShareProcessingDispatch>;

using FTSLikeDispatchResult =
    TTSPipelineDispatchResult<FTSLikeProcessingDispatch>;

using FTSRoomUserDispatchResult =
    TTSPipelineDispatchResult<FTSRoomUserProcessingDispatch>;

using FTSGiftDispatchResult =
    TTSPipelineDispatchResult<FTSGiftProcessingDispatch>;

using FTSMemberDispatchResult =
    TTSPipelineDispatchResult<FTSMemberProcessingDispatch>;

// Orquesta autoridades independientes sin asumir la semántica ni el ownership de ellas.
class FTSEventPipelineCoordinator final
{
public:
    explicit FTSEventPipelineCoordinator(
        FTSEventQueueSettings Settings = {},
        FTSNowProvider NowProvider = {}
    );

    FTSEventPipelineCoordinator(const FTSEventPipelineCoordinator&) = delete;
    FTSEventPipelineCoordinator& operator=(
        const FTSEventPipelineCoordinator&
    ) = delete;

    FTSEventPipelineCoordinator(FTSEventPipelineCoordinator&&) = delete;
    FTSEventPipelineCoordinator& operator=(
        FTSEventPipelineCoordinator&&
    ) = delete;

    [[nodiscard]]
    FTSUpdateFlowSettingsResult UpdateFlowSettings(
        ETSEventFlow Flow,
        const FTSFlowQueueSettings& NewSettings
    );

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitChat(FTSChatInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitFollow(FTSFollowInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitShare(FTSShareInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitLike(FTSLikeInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitRoomUser(FTSRoomUserInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitGift(FTSGiftInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitMember(FTSMemberInput Input);

    [[nodiscard]]
    FTSChatDispatchResult BeginChatProcessing();

    [[nodiscard]]
    FTSFollowDispatchResult BeginFollowProcessing();

    [[nodiscard]]
    FTSShareDispatchResult BeginShareProcessing();

    [[nodiscard]]
    FTSLikeDispatchResult BeginLikeProcessing();

    [[nodiscard]]
    FTSRoomUserDispatchResult BeginRoomUserProcessing();

    [[nodiscard]]
    FTSGiftDispatchResult BeginGiftProcessing();

    [[nodiscard]]
    FTSMemberDispatchResult BeginMemberProcessing();

    [[nodiscard]]
    FTSChatProcessingCompletionResult CompleteChatProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSFollowProcessingCompletionResult CompleteFollowProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSShareProcessingCompletionResult CompleteShareProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSLikeProcessingCompletionResult CompleteLikeProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSRoomUserProcessingCompletionResult CompleteRoomUserProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSGiftProcessingCompletionResult CompleteGiftProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSMemberProcessingCompletionResult CompleteMemberProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    FTSPumpResult Pump();

    [[nodiscard]]
    FTSProcessDueExpirationsResult ProcessDueExpirations();

    [[nodiscard]]
    FTSNextWakeTimeResult GetNextWakeTime();

    // Inspecciona el enrutamiento sin consumir ni autorizar el ready global.
    [[nodiscard]]
    std::optional<ETSEventFamilyKind> PeekPendingReadyFamilyKind() const;

    template <typename TCallback>
    [[nodiscard]]
    bool VisitEmissionBinding(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
        return BindingRegistry.Visit(
            EmissionId,
            std::forward<TCallback>(Callback)
        );
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitChatPayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Chat ||
            Binding.ExpectedFlow != ETSEventFlow::Chat)
        {
            throw std::logic_error("Emission binding is not a Chat binding");
        }

        const bool bFoundPayload = ChatPayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error("Chat binding references a missing payload");
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitFollowPayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Follow ||
            Binding.ExpectedFlow != ETSEventFlow::Follow)
        {
            throw std::logic_error("Emission binding is not a Follow binding");
        }

        const bool bFoundPayload = FollowPayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error("Follow binding references a missing payload");
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitSharePayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Share ||
            Binding.ExpectedFlow != ETSEventFlow::Share)
        {
            throw std::logic_error("Emission binding is not a Share binding");
        }

        const bool bFoundPayload = SharePayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error("Share binding references a missing payload");
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitLikePayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Like ||
            Binding.ExpectedFlow != ETSEventFlow::Like)
        {
            throw std::logic_error(
                "Emission binding is not a Like binding"
            );
        }

        const bool bFoundPayload = LikePayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error(
                "Like binding references a missing payload"
            );
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitRoomUserPayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::RoomUser ||
            Binding.ExpectedFlow != ETSEventFlow::RoomUser)
        {
            throw std::logic_error(
                "Emission binding is not a RoomUser binding"
            );
        }

        const bool bFoundPayload = RoomUserPayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error(
                "RoomUser binding references a missing payload"
            );
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitGiftPayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Gift ||
            Binding.ExpectedFlow != ETSEventFlow::Gift)
        {
            throw std::logic_error(
                "Emission binding is not a Gift binding"
            );
        }

        const bool bFoundPayload = GiftPayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error(
                "Gift binding references a missing payload"
            );
        }

        return true;
    }

    template <typename TCallback>
    [[nodiscard]]
    bool VisitMemberPayloadForEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
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
            return false;
        }

        if (Binding.FamilyKind != ETSEventFamilyKind::Member ||
            Binding.ExpectedFlow != ETSEventFlow::Member)
        {
            throw std::logic_error(
                "Emission binding is not a Member binding"
            );
        }

        const bool bFoundPayload = MemberPayloadRepository.Visit(
            Binding.PayloadHandle,
            std::forward<TCallback>(Callback)
        );

        if (!bFoundPayload)
        {
            throw std::logic_error(
                "Member binding references a missing payload"
            );
        }

        return true;
    }

    [[nodiscard]]
    std::size_t GetBindingCount() const noexcept;

    [[nodiscard]]
    std::size_t GetChatPayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetFollowPayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetSharePayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetLikePayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetRoomUserPayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetGiftPayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetMemberPayloadCount() const noexcept;

private:
    template <typename TPayload, typename TRepository>
    FTSPipelineAdmissionResult SubmitDecision(
        TTSFamilyDecision<TPayload> Decision,
        ETSEventFamilyKind ExpectedFamilyKind,
        ETSEventFlow ExpectedFlow,
        TRepository& Repository
    );

    template <typename TPayload, typename TDispatch, typename TRepository>
    TTSPipelineDispatchResult<TDispatch> BeginProcessing(
        ETSEventFamilyKind ExpectedFamilyKind,
        ETSEventFlow ExpectedFlow,
        TRepository& Repository
    );

    template <typename TRepository>
    FTSProcessingCompletionResult CompleteProcessing(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult,
        ETSEventFamilyKind ExpectedFamilyKind,
        ETSEventFlow ExpectedFlow,
        const TRepository& Repository
    );

    [[nodiscard]]
    FTSEmissionBinding ValidateReadyBinding(
        const FTSEmissionEnvelope& ReadyEmission
    ) const;

    void CaptureCorePumpOutcome(const FTSPumpOutcome& PumpOutcome);

    void ProcessPendingLifecycleEvents(
        const FTSEmissionLifecycleEvents& LifecycleEvents
    );

    void ProcessConfirmLifecycleEvents(
        FTSEmissionId EmissionId,
        const FTSEmissionLifecycleEvents& LifecycleEvents
    );

    void ProcessCancelLifecycleEvents(
        FTSEmissionId EmissionId,
        const FTSEmissionLifecycleEvents& LifecycleEvents
    );

    TikStudioEventQueueSystem Core;
    FTSChatPayloadRepository ChatPayloadRepository;
    FTSFollowPayloadRepository FollowPayloadRepository;
    FTSSharePayloadRepository SharePayloadRepository;
    FTSLikePayloadRepository LikePayloadRepository;
    FTSRoomUserPayloadRepository RoomUserPayloadRepository;
    FTSGiftPayloadRepository GiftPayloadRepository;
    FTSMemberPayloadRepository MemberPayloadRepository;
    FTSEmissionBindingRegistry BindingRegistry;
    // Las siete familias operativas comparten el ready porque el core posee un único
    // InFlight.
    std::optional<FTSEmissionEnvelope> PendingReadyEmission;
};
