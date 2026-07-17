#pragma once

#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

enum class ETSEventHostCommandKind : std::uint8_t
{
    None,
    ChatInput,
    FollowInput,
    ChatCompletion,
    FollowCompletion
};

// El variant sólo transporta el resultado propietario fuera del Host; no forma parte
// de los contratos del Pipeline ni del Core.
using FTSEventProcessingDispatch = std::variant<
    FTSChatProcessingDispatch,
    FTSFollowProcessingDispatch
>;

struct FTSEventHostCycleResult
{
    ETSEventHostCommandKind ProcessedCommand =
        ETSEventHostCommandKind::None;

    FTSProcessDueExpirationsResult DueExpirations{};

    // El tipo de comando determina cuál de estos resultados puede existir.
    std::optional<FTSPipelineAdmissionResult> AdmissionResult;
    std::optional<FTSProcessingCompletionResult> CompletionResult;

    std::optional<FTSPumpResult> PumpResult;
    std::optional<FTSEventProcessingDispatch> Dispatch;
    FTSNextWakeTimeResult NextWakeTime{};
    bool bMoreCommandsPending = false;
};

// Todas las familias comparten este owner thread, Coordinator y Core para preservar
// un único InFlight y un orden operativo global.
class FTSEventExecutionHost final
{
public:
    explicit FTSEventExecutionHost(
        FTSEventQueueSettings Settings = {},
        FTSNowProvider NowProvider = {}
    );

    ~FTSEventExecutionHost();

    FTSEventExecutionHost(const FTSEventExecutionHost&) = delete;
    FTSEventExecutionHost& operator=(const FTSEventExecutionHost&) = delete;

    FTSEventExecutionHost(FTSEventExecutionHost&&) = delete;
    FTSEventExecutionHost& operator=(FTSEventExecutionHost&&) = delete;

    [[nodiscard]]
    bool PostChat(FTSChatInput Input);

    [[nodiscard]]
    bool PostFollow(FTSFollowInput Input);

    [[nodiscard]]
    bool PostChatCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostFollowCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    // Sólo el hilo que construyó el Host puede ejecutar el Coordinator.
    [[nodiscard]]
    FTSEventHostCycleResult RunOneCycle();

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
