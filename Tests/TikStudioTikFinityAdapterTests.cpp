#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(42);

    RegisterTikFinityChatAdapterTests(Tests);
    RegisterTikFinityFollowAdapterTests(Tests);
    RegisterTikFinityShareAdapterTests(Tests);
    RegisterTikFinityLikeAdapterTests(Tests);
    RegisterTikFinityRoomUserAdapterTests(Tests);

    return RunTests(Tests);
}
