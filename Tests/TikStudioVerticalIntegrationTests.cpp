#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(5);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);
    RegisterShareVerticalIntegrationTests(Tests);
    RegisterLikeVerticalIntegrationTests(Tests);
    RegisterRoomUserVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
