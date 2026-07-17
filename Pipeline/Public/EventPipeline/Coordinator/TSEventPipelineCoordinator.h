#pragma once

#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Processing/TSChatProcessingCompletion.h"
#include "EventPipeline/Processing/TSChatProcessingDispatch.h"
#include "EventPipeline/Processing/TSFollowProcessingCompletion.h"
#include "EventPipeline/Processing/TSFollowProcessingDispatch.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"
#include "EventPipeline/Repositories/TSFollowPayloadRepository.h"
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
    FTSPipelineAdmissionResult SubmitChat(FTSChatInput Input);

    [[nodiscard]]
    FTSPipelineAdmissionResult SubmitFollow(FTSFollowInput Input);

    [[nodiscard]]
    FTSChatDispatchResult BeginChatProcessing();

    [[nodiscard]]
    FTSFollowDispatchResult BeginFollowProcessing();

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

    [[nodiscard]]
    std::size_t GetBindingCount() const noexcept;

    [[nodiscard]]
    std::size_t GetChatPayloadCount() const noexcept;

    [[nodiscard]]
    std::size_t GetFollowPayloadCount() const noexcept;

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
    FTSEmissionBindingRegistry BindingRegistry;
    // Chat y Follow comparten este ready porque el core posee un único InFlight.
    std::optional<FTSEmissionEnvelope> PendingReadyEmission;
};
