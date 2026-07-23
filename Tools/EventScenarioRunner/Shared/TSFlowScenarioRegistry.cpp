#include "Shared/TSFlowScenarioRegistry.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

void FTSFlowScenarioRegistry::Register(
    std::unique_ptr<ITSFlowScenarioProvider> Provider
)
{
    if (!Provider)
    {
        throw std::invalid_argument("Scenario provider cannot be null");
    }

    const ETSEventFlow Flow = Provider->GetObservedFlow();
    const std::string Name{Provider->GetDisplayName()};
    if (!IsValidFlow(Flow))
    {
        throw std::invalid_argument("Scenario provider flow is invalid");
    }

    if (Name.empty())
    {
        throw std::invalid_argument("Scenario provider name cannot be empty");
    }

    for (const std::unique_ptr<ITSFlowScenarioProvider>& Existing : Providers)
    {
        if (Existing->GetObservedFlow() == Flow)
        {
            throw std::invalid_argument(
                "A scenario provider already observes this flow"
            );
        }

        if (Existing->GetDisplayName() == std::string_view{Name})
        {
            throw std::invalid_argument(
                "A scenario provider already uses this name"
            );
        }
    }

    // El registro es la única fuente del menú: los flows reservados no aparecen
    // hasta que exista un provider operativo que pueda configurarlos y observarlos.
    Providers.push_back(std::move(Provider));
}

const std::vector<std::unique_ptr<ITSFlowScenarioProvider>>&
FTSFlowScenarioRegistry::GetProviders() const noexcept
{
    return Providers;
}
