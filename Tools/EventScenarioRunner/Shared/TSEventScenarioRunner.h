#pragma once

#include "Shared/TSFlowScenarioProvider.h"
#include "Shared/TSScenarioClock.h"
#include "Shared/TSScenarioReporter.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>

struct FTSActiveScenarioProcessing
{
    FTSEmissionId EmissionId = 0;
    std::chrono::milliseconds CompletionOffset{0};
    std::function<void(FTSEventExecutionHost&)> PostSucceededCompletion;
};

class FTSEventScenarioRunner final
{
public:
    FTSEventScenarioRunner(
        FTSScenarioDefinition Scenario,
        const ITSFlowScenarioProvider& Provider,
        FTSScenarioReporter& Reporter
    );

    void Run();

private:
    [[nodiscard]]
    static FTSScenarioDefinition ValidateAndOrderScenario(
        FTSScenarioDefinition Scenario,
        const ITSFlowScenarioProvider& Provider
    );

    [[nodiscard]]
    static std::chrono::milliseconds AddOffsets(
        std::chrono::milliseconds Left,
        std::chrono::milliseconds Right
    );

    [[nodiscard]]
    std::optional<std::chrono::milliseconds> GetNextEventOffset() const;

    [[nodiscard]]
    std::chrono::milliseconds ToScenarioOffset(
        FTSEventQueueTimePoint TimePoint
    ) const;

    void ExecuteDueCompletion();
    void ExecuteInputsAtCurrentOffset();
    void RunHostCycles();
    void RunMaintenanceCycle();
    void ProcessCycleResult(const FTSEventHostCycleResult& Result);
    void UpdateNextWake(const FTSNextWakeTimeResult& NextWake);

    [[nodiscard]]
    static std::size_t CountLifecycleEvents(
        const FTSEventHostCycleResult& Result
    ) noexcept;

    // La declaración conserva el lifetime: Clock se construye antes que Host y se
    // destruye después de que el Host deje de usar su NowProvider.
    FTSScenarioClock Clock;
    FTSScenarioDefinition Scenario;
    FTSEventExecutionHost Host;
    const ITSFlowScenarioProvider& Provider;
    FTSScenarioReporter& Reporter;
    std::size_t NextInputIndex = 0;
    std::optional<FTSActiveScenarioProcessing> ActiveProcessing;
    std::optional<std::chrono::milliseconds> NextWakeOffset;
    std::optional<std::chrono::milliseconds> RepeatedImmediateWake;
    std::optional<FTSEmissionId> ExpectedCompletionEmissionId;
    bool bMoreCommandsPending = false;
    bool bLastCycleProducedDispatch = false;
};
