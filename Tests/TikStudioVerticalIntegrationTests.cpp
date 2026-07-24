#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(9);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);
    RegisterShareVerticalIntegrationTests(Tests);
    RegisterShareMilestoneVerticalIntegrationTests(Tests);
    RegisterLikeVerticalIntegrationTests(Tests);
    RegisterRoomUserVerticalIntegrationTests(Tests);
    RegisterGiftVerticalIntegrationTests(Tests);
    RegisterGiftComboVerticalIntegrationTests(Tests);
    RegisterMemberVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
