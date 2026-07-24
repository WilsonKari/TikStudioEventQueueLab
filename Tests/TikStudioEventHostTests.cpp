#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(93);

    RegisterHostInfrastructureTests(Tests);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterShareMilestoneHostTests(Tests);
    RegisterLikeHostTests(Tests);
    RegisterRoomUserHostTests(Tests);
    RegisterGiftHostTests(Tests);
    RegisterGiftComboHostTests(Tests);
    RegisterMemberHostTests(Tests);

    return RunTests(Tests);
}
