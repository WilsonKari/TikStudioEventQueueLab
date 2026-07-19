#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(49);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterLikeHostTests(Tests);
    RegisterRoomUserHostTests(Tests);
    RegisterGiftHostTests(Tests);

    return RunTests(Tests);
}
