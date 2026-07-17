#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(42);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterPipelineInfrastructureTests(Tests);
    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
