#include "EventPipeline/Families/TSShareMilestoneFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void MutateShareInput(
        FTSShareInput& Input,
        const char* UniqueId,
        const char* Nickname,
        const char* ProfilePictureUrl,
        std::int32_t Offset
    )
    {
        Input.User.UniqueId = UniqueId;
        Input.User.Nickname = Nickname;
        Input.User.ProfilePictureUrl = ProfilePictureUrl;
        Input.User.FollowRole = 20 + Offset;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 30 + Offset;
        Input.User.GifterLevel = 40 + Offset;
        Input.User.TeamMemberLevel = 50 + Offset;
    }

    void TestShareMilestoneProducesDirectShareMilestoneCandidate()
    {
        const FTSShareInput Input = MakeCompleteShareInput();
        const TTSFamilyDecision<FTSShareMilestonePayload> Decision =
            FTSShareMilestoneFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "ShareMilestone must produce a structural candidate"
        );
        Require(
            Decision->FamilyKind == ETSEventFamilyKind::Share &&
                Decision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone &&
                Decision->EnqueueRequest.Flow != ETSEventFlow::Share,
            "ShareMilestone candidate must use Share/ShareMilestone"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Share,
                ETSEventFlow::Share
            ),
            "Direct Share must remain operationally authorized"
        );
        Require(
            !IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Share,
                ETSEventFlow::ShareMilestone
            ),
            "Structural ShareMilestone must remain operationally unauthorized"
        );
        Require(
            Decision->EnqueueRequest.PriorityAdjustment == 0 &&
                !Decision->EnqueueRequest.bOverrideTTL &&
                Decision->EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0} &&
                !Decision->EnqueueRequest.bProtectedFromEviction,
            "ShareMilestone must preserve neutral admission defaults"
        );
        RequireShareInputEqual(
            Decision->Payload.Input,
            Input,
            "ShareMilestone structural payload"
        );
    }

    void TestShareMilestonePayloadSnapshotAndCallIndependence()
    {
        FTSShareInput FirstInput = MakeCompleteShareInput();
        const FTSShareInput FirstExpected = FirstInput;
        const TTSFamilyDecision<FTSShareMilestonePayload> FirstDecision =
            FTSShareMilestoneFamily::Decide(FirstInput);

        Require(
            FirstDecision.has_value(),
            "First ShareMilestone decision must produce a candidate"
        );
        RequireShareInputEqual(
            FirstInput,
            FirstExpected,
            "First caller remains unchanged during Decide"
        );

        MutateShareInput(
            FirstInput,
            "first-mutated-user",
            "First Mutated User",
            "https://example.test/first-mutated.png",
            1
        );
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First ShareMilestone payload remains an owned snapshot"
        );

        FTSShareInput SecondInput = MakeCompleteShareInput();
        MutateShareInput(
            SecondInput,
            "second-user",
            "Second User",
            "https://example.test/second.png",
            2
        );
        const FTSShareInput SecondExpected = SecondInput;
        const TTSFamilyDecision<FTSShareMilestonePayload> SecondDecision =
            FTSShareMilestoneFamily::Decide(SecondInput);

        Require(
            SecondDecision.has_value() &&
                FirstDecision->FamilyKind == ETSEventFamilyKind::Share &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Share &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone,
            "Independent decisions must select only Share/ShareMilestone"
        );
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First decision remains independent"
        );
        RequireShareInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second decision owns only its input"
        );

        MutateShareInput(
            SecondInput,
            "second-mutated-user",
            "Second Mutated User",
            "https://example.test/second-mutated.png",
            3
        );
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First payload remains independent after second caller mutation"
        );
        RequireShareInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second payload remains independent after caller mutation"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareMilestonePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "ShareMilestone produces direct Share/ShareMilestone candidate",
            &TestShareMilestoneProducesDirectShareMilestoneCandidate
        });
        Tests.push_back({
            "ShareMilestone payload snapshot and call independence",
            &TestShareMilestonePayloadSnapshotAndCallIndependence
        });
    }
}
