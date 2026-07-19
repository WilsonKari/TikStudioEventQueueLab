#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(86);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterSharePipelineFamilyTests(Tests);
    RegisterLikePipelineFamilyTests(Tests);
    RegisterRoomUserPipelineFamilyTests(Tests);
    RegisterGiftPipelineFamilyTests(Tests);

    RegisterPipelineInfrastructureTests(Tests);

    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);
    RegisterSharePipelineCoordinatorTests(Tests);
    RegisterLikePipelineCoordinatorTests(Tests);
    RegisterRoomUserPipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
