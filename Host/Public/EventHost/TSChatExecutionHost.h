#pragma once

#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include <cstdint>
#include <memory>
#include <optional>

enum class ETSChatHostCommandKind : std::uint8_t
{
    None,
    ChatInput,
    ProcessingCompletion
};

struct FTSChatHostCycleResult
{
    ETSChatHostCommandKind ProcessedCommand =
        ETSChatHostCommandKind::None;

    // Terminales Pending procesados por mantenimiento después del comando.
    FTSProcessDueExpirationsResult DueExpirations{};

    // Sólo una de estas opciones puede existir para el comando procesado.
    std::optional<FTSPipelineAdmissionResult> AdmissionResult;
    std::optional<FTSChatProcessingCompletionResult> CompletionResult;

    // Existe únicamente cuando el Host necesitó avanzar el core explícitamente.
    std::optional<FTSPumpResult> PumpResult;

    // El despacho es propietario y como máximo existe uno por ciclo.
    std::optional<FTSChatProcessingDispatch> Dispatch;

    // El scheduler externo decide cómo y cuándo utilizar este instante.
    FTSNextWakeTimeResult NextWakeTime{};

    // Refleja comandos que permanecen publicados al terminar el ciclo.
    bool bMoreCommandsPending = false;
};

// Serializa el Pipeline en el hilo propietario sin crear hilos ni timers internos.
class FTSChatExecutionHost final
{
public:
    explicit FTSChatExecutionHost(
        FTSEventQueueSettings Settings = {},
        FTSNowProvider NowProvider = {}
    );

    ~FTSChatExecutionHost();

    FTSChatExecutionHost(const FTSChatExecutionHost&) = delete;
    FTSChatExecutionHost& operator=(const FTSChatExecutionHost&) = delete;

    FTSChatExecutionHost(FTSChatExecutionHost&&) = delete;
    FTSChatExecutionHost& operator=(FTSChatExecutionHost&&) = delete;

    // Puede llamarse desde cualquier hilo. El bool sólo señala una transición de la
    // bandeja de vacía a ocupada; no representa el resultado de admisión.
    [[nodiscard]]
    bool PostChat(FTSChatInput Input);

    // Puede llamarse desde cualquier hilo. Valida argumentos portables, pero difiere
    // toda consulta al Pipeline hasta el ciclo.
    [[nodiscard]]
    bool PostChatCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    // Sólo el hilo que construyó el Host puede ejecutar el coordinador.
    [[nodiscard]]
    FTSChatHostCycleResult RunOneCycle();

private:
    class FImpl;

    std::unique_ptr<FImpl> Impl;
};
