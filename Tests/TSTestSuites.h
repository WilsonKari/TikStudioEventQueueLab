#pragma once

#include "TSTestHarness.h"

namespace TikStudio::Tests
{
    void RegisterCommonUserPriorityPolicyTests(FTSTestCases& Tests);

    void RegisterChatPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterChatPipelineCoordinatorTests(FTSTestCases& Tests);
    void RegisterChatSemanticTests(FTSTestCases& Tests);

    void RegisterFollowPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterFollowPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterSharePipelineFamilyTests(FTSTestCases& Tests);
    void RegisterSharePipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterLikePipelineFamilyTests(FTSTestCases& Tests);
    void RegisterLikePipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterRoomUserPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterRoomUserPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterGiftPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterGiftPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterMemberPipelineFamilyTests(FTSTestCases& Tests);
    void RegisterMemberPipelineCoordinatorTests(FTSTestCases& Tests);

    void RegisterPipelineInfrastructureTests(FTSTestCases& Tests);
    void RegisterHostInfrastructureTests(FTSTestCases& Tests);

    void RegisterChatHostTests(FTSTestCases& Tests);
    void RegisterFollowHostTests(FTSTestCases& Tests);
    void RegisterShareHostTests(FTSTestCases& Tests);
    void RegisterLikeHostTests(FTSTestCases& Tests);
    void RegisterRoomUserHostTests(FTSTestCases& Tests);
    void RegisterGiftHostTests(FTSTestCases& Tests);
    void RegisterMemberHostTests(FTSTestCases& Tests);
    void RegisterChatVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterFollowVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterShareVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterLikeVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterRoomUserVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterGiftVerticalIntegrationTests(FTSTestCases& Tests);
    void RegisterMemberVerticalIntegrationTests(FTSTestCases& Tests);

    void RegisterTikFinityChatAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityFollowAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityShareAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityLikeAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityRoomUserAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityGiftAdapterTests(FTSTestCases& Tests);
    void RegisterTikFinityMemberAdapterTests(FTSTestCases& Tests);
}
