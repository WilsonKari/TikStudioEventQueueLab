#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(112);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterSharePipelineFamilyTests(Tests);
    RegisterLikePipelineFamilyTests(Tests);
    RegisterRoomUserPipelineFamilyTests(Tests);
    RegisterGiftPipelineFamilyTests(Tests);
    RegisterMemberPipelineFamilyTests(Tests);

    RegisterPipelineInfrastructureTests(Tests);

    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);
    RegisterSharePipelineCoordinatorTests(Tests);
    RegisterLikePipelineCoordinatorTests(Tests);
    RegisterRoomUserPipelineCoordinatorTests(Tests);
    RegisterGiftPipelineCoordinatorTests(Tests);
    RegisterMemberPipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
