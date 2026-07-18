#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(32);

    RegisterTikFinityChatAdapterTests(Tests);
    RegisterTikFinityFollowAdapterTests(Tests);
    RegisterTikFinityShareAdapterTests(Tests);
    RegisterTikFinityLikeAdapterTests(Tests);

    return RunTests(Tests);
}
