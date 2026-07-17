#pragma once

#include "TSTestHarness.h"

namespace TikStudio::Tests
{
    void RegisterChatPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterChatPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterFollowPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterFollowPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterPipelineInfrastructureTests(FTSTestCases& Tests);

    void RegisterChatHostTests(FTSTestCases& Tests);

    void RegisterTikFinityChatAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityFollowAdapterTests(FTSTestCases& Tests);
}
