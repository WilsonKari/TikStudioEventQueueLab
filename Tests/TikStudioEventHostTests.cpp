#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(18);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterFollowVerticalIntegrationTests(Tests);

    return RunTests(Tests);
}
