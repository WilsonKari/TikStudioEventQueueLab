#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(4);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);
    RegisterShareVerticalIntegrationTests(Tests);
    RegisterLikeVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
