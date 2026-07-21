#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(68);

    RegisterHostInfrastructureTests(Tests);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterLikeHostTests(Tests);
    RegisterRoomUserHostTests(Tests);
    RegisterGiftHostTests(Tests);
    RegisterMemberHostTests(Tests);

    return RunTests(Tests);
}
