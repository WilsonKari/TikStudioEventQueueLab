#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(17);

    RegisterTikFinityChatAdapterTests(Tests);
    RegisterTikFinityFollowAdapterTests(Tests);

    return RunTests(Tests);
}
