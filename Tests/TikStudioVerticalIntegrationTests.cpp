#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(2);

    RegisterChatVerticalIntegrationTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
