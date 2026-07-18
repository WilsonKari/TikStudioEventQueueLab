#include "EventPipeline/Families/TSShareFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <string>

namespace
{
    using namespace TikStudio::Tests;

    void TestShareCandidateAndAdmissionDefaults()
    {
        FTSShareInput Input;
        Input.User.UniqueId = "share-user";

        const TTSFamilyDecision<FTSSharePayload> Decision =
            FTSShareFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Share must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSSharePayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Share,
            "Share candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Share,
            "Share candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::ShareMilestone,
            "Share candidate must not infer a milestone"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Share candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Share candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride.count() == 0,
            "Share candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Share candidate must not request special eviction protection"
        );
    }

    void TestSharePayloadSnapshotAndInputPreservation()
    {
        FTSShareInput Input = MakeCompleteShareInput();
        const FTSShareInput Original = Input;

        const TTSFamilyDecision<FTSSharePayload> Decision =
            FTSShareFamily::Decide(Input);

        Require(Decision.has_value(), "Share snapshot must produce a candidate");
        RequireShareInputEqual(
            Input,
            Original,
            "Share input after by-copy decision"
        );

        Input.User.UniqueId = "mutated-share-user";
        Input.User.Nickname = "Mutated Share User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireShareInputEqual(
            Decision->Payload.Input,
            Original,
            "Share payload snapshot"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterSharePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Share candidate and admission defaults", &TestShareCandidateAndAdmissionDefaults});
        Tests.push_back({"Share payload snapshot and input preservation", &TestSharePayloadSnapshotAndInputPreservation});
    }
}
