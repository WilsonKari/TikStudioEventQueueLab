#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(41);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterLikeHostTests(Tests);
    RegisterRoomUserHostTests(Tests);

    return RunTests(Tests);
}
