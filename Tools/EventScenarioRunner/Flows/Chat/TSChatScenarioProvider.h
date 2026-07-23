#pragma once

#include "Shared/TSFlowScenarioProvider.h"

#include <string_view>

class FTSChatScenarioProvider final : public ITSFlowScenarioProvider
{
public:
    [[nodiscard]]
    ETSEventFlow GetObservedFlow() const noexcept override;

    [[nodiscard]]
    std::string_view GetDisplayName() const noexcept override;

    [[nodiscard]]
    FTSScenarioDefinition Configure(
        FTSScenarioConsole& Console
    ) const override;

    [[nodiscard]]
    FTSObservedScenarioDispatch ObserveDispatch(
        const FTSEventProcessingDispatch& Dispatch
    ) const override;
};
