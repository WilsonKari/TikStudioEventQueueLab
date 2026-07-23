#pragma once

#include "Shared/TSScenarioContracts.h"

#include <chrono>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class ITSFlowScenarioProvider;

class FTSScenarioReporter final
{
public:
    explicit FTSScenarioReporter(std::ostream& Output) noexcept;

    void ReportScenarioConfiguration(
        const ITSFlowScenarioProvider& Provider,
        const FTSScenarioDefinition& Scenario
    );

    void ReportInputPublished(
        std::chrono::milliseconds Offset,
        const FTSScheduledScenarioInput& Input
    );

    void ReportCycle(
        std::chrono::milliseconds Offset,
        const FTSEventHostCycleResult& Result,
        FTSEventQueueTimePoint Origin
    );

    void ReportDispatch(
        std::chrono::milliseconds Offset,
        const FTSObservedScenarioDispatch& Dispatch,
        FTSEventQueueTimePoint Origin
    );

    void ReportCompletionScheduled(
        FTSEmissionId EmissionId,
        std::chrono::milliseconds CompletionOffset
    );

    void ReportSummary();

private:
    void ReportLifecycleEvents(
        std::string_view Source,
        const FTSEmissionLifecycleEvents& Events,
        FTSEventQueueTimePoint Origin
    );

    std::ostream& Output;
    std::size_t PublishedInputs = 0;
    std::size_t AcceptedAdmissions = 0;
    std::size_t AccumulatedAdmissions = 0;
    std::map<std::string, std::size_t> RejectedAdmissions;
    std::map<std::string, std::size_t> ExpirationsByReason;
    std::size_t Dispatches = 0;
    std::size_t Confirmations = 0;
    std::vector<FTSEmissionId> ProcessingOrder;
};
