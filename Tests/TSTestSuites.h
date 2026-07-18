#pragma once

#include "TSTestHarness.h"

namespace TikStudio::Tests
{
    void RegisterChatPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterChatPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterFollowPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterFollowPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterSharePipelineFamilyTests(FTSTestCases& Tests);
    void RegisterSharePipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterLikePipelineFamilyTests(FTSTestCases& Tests);

    void RegisterPipelineInfrastructureTests(FTSTestCases& Tests);

    void RegisterChatHostTests(FTSTestCases& Tests);
    void RegisterFollowHostTests(FTSTestCases& Tests);
    void RegisterShareHostTests(FTSTestCases& Tests);
    void RegisterChatVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterFollowVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterShareVerticalIntegrationTests(FTSTestCases& Tests);

    void RegisterTikFinityChatAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityFollowAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityShareAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityLikeAdapterTests(FTSTestCases& Tests);
}
