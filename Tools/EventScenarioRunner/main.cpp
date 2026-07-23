#include "Flows/Chat/TSChatScenarioProvider.h"
#include "Shared/TSEventScenarioRunner.h"
#include "Shared/TSFlowScenarioRegistry.h"
#include "Shared/TSScenarioConsole.h"
#include "Shared/TSScenarioReporter.h"

#include <exception>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

int main()
{
    try
    {
        FTSScenarioConsole Console(std::cin, std::cout);
        FTSFlowScenarioRegistry Registry;
        Registry.Register(std::make_unique<FTSChatScenarioProvider>());

        while (true)
        {
            Console.WriteLine("\n=== TikStudio Event Scenario Runner ===");
            const auto& Providers = Registry.GetProviders();
            for (std::size_t Index = 0; Index < Providers.size(); ++Index)
            {
                Console.WriteLine(
                    std::to_string(Index + 1) + ". " +
                    std::string(Providers[Index]->GetDisplayName())
                );
            }
            Console.WriteLine("0. Salir");

            const std::size_t Selection = Console.ReadMenuSelection(
                "Selecciona una opción: ",
                Providers.size()
            );
            if (Selection == 0)
            {
                Console.WriteLine("Hasta luego.");
                return 0;
            }

            const ITSFlowScenarioProvider& Provider =
                *Providers[Selection - 1];
            FTSScenarioDefinition Scenario = Provider.Configure(Console);
            FTSScenarioReporter Reporter(std::cout);
            Reporter.ReportScenarioConfiguration(Provider, Scenario);

            if (!Console.ReadYesNo(
                    "\n¿Ejecutar este escenario? [S/n]: ",
                    true
                ))
            {
                Console.WriteLine("Escenario descartado.");
                continue;
            }

            FTSEventScenarioRunner Runner(
                std::move(Scenario),
                Provider,
                Reporter
            );
            Runner.Run();
        }
    }
    catch (const FTSScenarioInputCancelled& Error)
    {
        std::cout << '\n' << Error.what() << '\n';
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "Error: " << Error.what() << '\n';
        return 1;
    }
}
