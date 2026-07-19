#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(6);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);
    RegisterShareVerticalIntegrationTests(Tests);
    RegisterLikeVerticalIntegrationTests(Tests);
    RegisterRoomUserVerticalIntegrationTests(Tests);
    RegisterGiftVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
