#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(58);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterSharePipelineFamilyTests(Tests);
    RegisterLikePipelineFamilyTests(Tests);

    RegisterPipelineInfrastructureTests(Tests);

    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);
    RegisterSharePipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
