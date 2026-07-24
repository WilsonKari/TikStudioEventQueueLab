#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(8);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);
    RegisterShareVerticalIntegrationTests(Tests);
    RegisterLikeVerticalIntegrationTests(Tests);
    RegisterRoomUserVerticalIntegrationTests(Tests);
    RegisterGiftVerticalIntegrationTests(Tests);
    RegisterGiftComboVerticalIntegrationTests(Tests);
    RegisterMemberVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
