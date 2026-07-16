#pragma once

#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"
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

    [[nodiscard]]
    std::size_t GetBindingCount() const noexcept;

    [[nodiscard]]
    std::size_t GetChatPayloadCount() const noexcept;

private:
    void ProcessEnqueueLifecycleEvents(
        const FTSEmissionLifecycleEvents& LifecycleEvents
    );

    TikStudioEventQueueSystem Core;
    FTSChatPayloadRepository ChatPayloadRepository;
    FTSEmissionBindingRegistry BindingRegistry;
};
