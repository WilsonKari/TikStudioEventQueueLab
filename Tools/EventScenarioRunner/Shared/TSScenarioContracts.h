#pragma once

#include "EventHost/TSEventExecutionHost.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct FTSScenarioRuntimeSettings
{
    std::chrono::milliseconds ProcessingDuration{10000};
    std::chrono::milliseconds DefaultArrivalInterval{2000};
};

struct FTSScheduledScenarioInput
{
    std::uint64_t Sequence = 0;
    std::chrono::milliseconds ArrivalOffset{0};
    std::string Description;
    // El booleano de Post* sólo indica si el FIFO estaba vacío; la admisión real
    // se observa después en FTSEventHostCycleResult::AdmissionResult.
    std::function<void(FTSEventExecutionHost&)> PostAction;
};

struct FTSScenarioDefinition
{
    FTSEventQueueSettings CoreSettings;
    FTSEventPipelineSettings PipelineSettings;
    FTSScenarioRuntimeSettings RuntimeSettings;
    std::vector<FTSScheduledScenarioInput> Inputs;
    // Permite que cada provider describa su configuración sin filtrarla al motor.
    std::vector<std::string> ConfigurationDetailLines;
};

struct FTSObservedScenarioDispatch
{
    FTSEmissionEnvelope Emission;
    std::vector<std::string> DetailLines;
    // Type erasure mantiene al motor independiente del completion tipado de cada flow.
    std::function<void(FTSEventExecutionHost&)> PostSucceededCompletion;
};
