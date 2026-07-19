#include "EventPipeline/Families/TSGiftFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <utility>

namespace
{
    using namespace TikStudio::Tests;

    void TestGiftCandidateAndAdmissionDefaults()
    {
        const FTSGiftInput Input = MakeCompleteGiftInput();
        const TTSFamilyDecision<FTSGiftPayload> Decision =
            FTSGiftFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Gift must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSGiftPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Gift,
            "Gift candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Gift,
            "Gift candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::GiftCombo,
            "Gift repeat metadata must not select GiftCombo"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Gift metadata must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Gift candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "Gift candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Gift candidate must not request eviction protection"
        );
        RequireGiftInputEqual(
            Candidate.Payload.Input,
            Input,
            "Gift candidate payload"
        );
    }

    void TestGiftPayloadSnapshotAndInputPreservation()
    {
        FTSGiftInput Input = MakeCompleteGiftInput();
        const FTSGiftInput Original = Input;

        const TTSFamilyDecision<FTSGiftPayload> Decision =
            FTSGiftFamily::Decide(Input);

        Require(Decision.has_value(), "Gift snapshot must produce a candidate");
        RequireGiftInputEqual(
            Input,
            Original,
            "Gift input after by-copy decision"
        );

        Input.GiftId = 0;
        Input.GiftName = "Mutated Gift";
        Input.GiftPictureUrl.clear();
        Input.DiamondCount = 0;
        Input.RepeatCount = 0;
        Input.GiftType = 0;
        Input.Describe = "Mutated description";
        Input.bRepeatEnd = false;
        Input.GroupId.clear();
        Input.User.UniqueId = "mutated-gift-user";
        Input.User.Nickname = "Mutated Gift User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireGiftInputEqual(
            Decision->Payload.Input,
            Original,
            "Gift payload snapshot"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Gift candidate and admission defaults",
            &TestGiftCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "Gift payload snapshot and input preservation",
            &TestGiftPayloadSnapshotAndInputPreservation
        });
    }
}
