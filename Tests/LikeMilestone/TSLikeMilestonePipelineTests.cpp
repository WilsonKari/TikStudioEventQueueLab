#include "EventPipeline/Families/TSLikeMilestoneFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void MutateLikeInput(
        FTSLikeInput& Input,
        std::int32_t LikeCount,
        std::int32_t TotalLikeCount,
        const char* UniqueId,
        const char* Nickname,
        const char* ProfilePictureUrl,
        std::int32_t Offset
    )
    {
        Input.LikeCount = LikeCount;
        Input.TotalLikeCount = TotalLikeCount;
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

    void TestLikeMilestoneProducesDirectLikeMilestoneCandidate()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 37;
        Input.TotalLikeCount = 12345;

        const TTSFamilyDecision<FTSLikeMilestonePayload> Decision =
            FTSLikeMilestoneFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "LikeMilestone must produce a structural candidate"
        );
        Require(
            Decision->FamilyKind == ETSEventFamilyKind::Like &&
                Decision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone &&
                Decision->EnqueueRequest.Flow != ETSEventFlow::Like,
            "LikeMilestone candidate must use Like/LikeMilestone"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Like,
                ETSEventFlow::Like
            ),
            "Direct Like must remain operationally authorized"
        );
        Require(
            !IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Like,
                ETSEventFlow::LikeMilestone
            ),
            "Like/LikeMilestone must remain structurally reserved in phase A"
        );
        Require(
            Decision->EnqueueRequest.PriorityAdjustment == 0 &&
                !Decision->EnqueueRequest.bOverrideTTL &&
                Decision->EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0} &&
                !Decision->EnqueueRequest.bProtectedFromEviction,
            "LikeMilestone must preserve neutral admission defaults"
        );
        RequireLikeInputEqual(
            Decision->Payload.Input,
            Input,
            "LikeMilestone structural payload"
        );
    }

    void TestLikeMilestonePayloadSnapshotAndCallIndependence()
    {
        FTSLikeInput FirstInput = MakeCompleteLikeInput();
        FirstInput.LikeCount = 41;
        FirstInput.TotalLikeCount = 9000;
        const FTSLikeInput FirstExpected = FirstInput;
        const TTSFamilyDecision<FTSLikeMilestonePayload> FirstDecision =
            FTSLikeMilestoneFamily::Decide(FirstInput);

        Require(
            FirstDecision.has_value(),
            "First LikeMilestone decision must produce a candidate"
        );
        RequireLikeInputEqual(
            FirstInput,
            FirstExpected,
            "First caller remains unchanged during Decide"
        );

        MutateLikeInput(
            FirstInput,
            101,
            15000,
            "first-mutated-like-user",
            "First Mutated Like User",
            "https://example.test/first-mutated-like.png",
            1
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First LikeMilestone payload remains an owned snapshot"
        );

        FTSLikeInput SecondInput = MakeCompleteLikeInput();
        MutateLikeInput(
            SecondInput,
            13,
            700,
            "second-like-user",
            "Second Like User",
            "https://example.test/second-like.png",
            2
        );
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        const FTSLikeInput SecondExpected = SecondInput;
        const TTSFamilyDecision<FTSLikeMilestonePayload> SecondDecision =
            FTSLikeMilestoneFamily::Decide(SecondInput);

        Require(
            SecondDecision.has_value() &&
                FirstDecision->FamilyKind == ETSEventFamilyKind::Like &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Like &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone,
            "Independent decisions must select only Like/LikeMilestone"
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First decision remains independent"
        );
        RequireLikeInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second decision owns only its input"
        );

        MutateLikeInput(
            SecondInput,
            303,
            25000,
            "second-mutated-like-user",
            "Second Mutated Like User",
            "https://example.test/second-mutated-like.png",
            3
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First payload remains independent after second caller mutation"
        );
        RequireLikeInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second payload remains independent after caller mutation"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikeMilestonePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "LikeMilestone produces direct Like/LikeMilestone candidate",
            &TestLikeMilestoneProducesDirectLikeMilestoneCandidate
        });
        Tests.push_back({
            "LikeMilestone payload snapshot and call independence",
            &TestLikeMilestonePayloadSnapshotAndCallIndependence
        });
    }
}
