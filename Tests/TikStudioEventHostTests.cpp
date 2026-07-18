#include "TSTestHarness.h"
#include "TSTestSuites.h"

int main()
{
    using namespace TikStudio::Tests;

    FTSTestCases Tests;
    Tests.reserve(33);

    RegisterChatHostTests(Tests);
    RegisterFollowHostTests(Tests);
    RegisterShareHostTests(Tests);
    RegisterLikeHostTests(Tests);

    return RunTests(Tests);
}
