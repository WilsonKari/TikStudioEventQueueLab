#pragma once

#include "Shared/TSFlowScenarioProvider.h"

#include <memory>
#include <vector>

class FTSFlowScenarioRegistry final
{
public:
    void Register(std::unique_ptr<ITSFlowScenarioProvider> Provider);

    [[nodiscard]]
    const std::vector<std::unique_ptr<ITSFlowScenarioProvider>>&
    GetProviders() const noexcept;

private:
    std::vector<std::unique_ptr<ITSFlowScenarioProvider>> Providers;
};
