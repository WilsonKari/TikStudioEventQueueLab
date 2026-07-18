#include "EventPipeline/Families/TSLikeFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void TestLikeCandidateAndAdmissionDefaults()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 250;
        Input.TotalLikeCount = 500000;

        const TTSFamilyDecision<FTSLikePayload> Decision =
            FTSLikeFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Like must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSLikePayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Like,
            "Like candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Like,
            "Like candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::LikeUser,
            "Like counters must not select LikeUser"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Like counters must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Like candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "Like candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Like candidate must not request eviction protection"
        );
    }

    void TestLikePayloadSnapshotAndInputPreservation()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        const FTSLikeInput Original = Input;

        const TTSFamilyDecision<FTSLikePayload> Decision =
            FTSLikeFamily::Decide(Input);

        Require(Decision.has_value(), "Like snapshot must produce a candidate");
        RequireLikeInputEqual(
            Input,
            Original,
            "Like input after by-copy decision"
        );

        Input.LikeCount = 0;
        Input.TotalLikeCount = 0;
        Input.User.UniqueId = "mutated-like-user";
        Input.User.Nickname = "Mutated Like User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireLikeInputEqual(
            Decision->Payload.Input,
            Original,
            "Like payload snapshot"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Like candidate and admission defaults",
            &TestLikeCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "Like payload snapshot and input preservation",
            &TestLikePayloadSnapshotAndInputPreservation
        });
    }
}
