#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(200);

    RegisterCommonUserPriorityPolicyTests(Tests);

    RegisterChatPipelineFamilyTests(Tests);
    RegisterChatSemanticTests(Tests);
    RegisterFollowPipelineFamilyTests(Tests);
    RegisterSharePipelineFamilyTests(Tests);
    RegisterShareMilestonePipelineFamilyTests(Tests);
    RegisterLikePipelineFamilyTests(Tests);
    RegisterRoomUserPipelineFamilyTests(Tests);
    RegisterGiftPipelineFamilyTests(Tests);
    RegisterGiftComboPipelineFamilyTests(Tests);
    RegisterMemberPipelineFamilyTests(Tests);

    RegisterPipelineInfrastructureTests(Tests);

    RegisterChatPipelineCoordinatorTests(Tests);
    RegisterFollowPipelineCoordinatorTests(Tests);
    RegisterSharePipelineCoordinatorTests(Tests);
    RegisterShareMilestonePipelineCoordinatorTests(Tests);
    RegisterLikePipelineCoordinatorTests(Tests);
    RegisterRoomUserPipelineCoordinatorTests(Tests);
    RegisterGiftPipelineCoordinatorTests(Tests);
    RegisterGiftComboPipelineCoordinatorTests(Tests);
    RegisterMemberPipelineCoordinatorTests(Tests);

    return RunTests(Tests);
}
