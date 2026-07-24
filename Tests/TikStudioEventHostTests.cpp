#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(83);

    RegisterHostInfrastructureTests(Tests);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterLikeHostTests(Tests);
    RegisterRoomUserHostTests(Tests);
    RegisterGiftHostTests(Tests);
    RegisterGiftComboHostTests(Tests);
    RegisterMemberHostTests(Tests);

    return RunTests(Tests);
}
