#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(100);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterSharePipelineFamilyTests(Tests);
    RegisterLikePipelineFamilyTests(Tests);
    RegisterRoomUserPipelineFamilyTests(Tests);
    RegisterGiftPipelineFamilyTests(Tests);
    RegisterMemberPipelineTests(Tests);

    RegisterPipelineInfrastructureTests(Tests);

    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);
    RegisterSharePipelineCoordinatorTests(Tests);
    RegisterLikePipelineCoordinatorTests(Tests);
    RegisterRoomUserPipelineCoordinatorTests(Tests);
    RegisterGiftPipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
