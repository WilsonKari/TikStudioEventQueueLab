#pragma once

#include "Shared/TSScenarioContracts.h"

#include <string_view>

class FTSScenarioConsole;

class ITSFlowScenarioProvider
{
public:
    virtual ~ITSFlowScenarioProvider() = default;

    [[nodiscard]]
    virtual ETSEventFlow GetObservedFlow() const noexcept = 0;

    [[nodiscard]]
    virtual std::string_view GetDisplayName() const noexcept = 0;

    [[nodiscard]]
    virtual FTSScenarioDefinition Configure(
        FTSScenarioConsole& Console
    ) const = 0;

    [[nodiscard]]
    virtual FTSObservedScenarioDispatch ObserveDispatch(
        const FTSEventProcessingDispatch& Dispatch
    ) const = 0;
};
