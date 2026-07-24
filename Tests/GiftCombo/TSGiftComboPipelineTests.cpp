#include "EventPipeline/Families/TSGiftComboFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void TestGiftComboProducesDirectGiftComboCandidate()
    {
        FTSGiftInput Input = MakeCompleteGiftInput();
        Input.DiamondCount = 777;
        Input.RepeatCount = 12;
        Input.GiftType = 7;
        Input.bRepeatEnd = true;
        Input.GroupId = "gift-combo-direct-group";

        const TTSFamilyDecision<FTSGiftComboPayload> Decision =
            FTSGiftComboFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "GiftCombo must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSGiftComboPayload>& Candidate =
            *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Gift,
            "GiftCombo must remain in the Gift domain"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::GiftCombo &&
                Candidate.EnqueueRequest.Flow != ETSEventFlow::Gift,
            "GiftCombo candidate Flow mismatch"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Gift,
                ETSEventFlow::Gift
            ),
            "Gift/Gift must remain authorized"
        );
        Require(
            !IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Gift,
                ETSEventFlow::GiftCombo
            ),
            "Gift/GiftCombo must remain unauthorized in phase A"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "GiftCombo candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL &&
                Candidate.EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0},
            "GiftCombo candidate TTL defaults mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "GiftCombo candidate must not request eviction protection"
        );
        RequireGiftInputEqual(
            Candidate.Payload.Input,
            Input,
            "GiftCombo direct payload"
        );
    }

    void TestGiftComboPayloadSnapshotAndMetadataIndependence()
    {
        FTSGiftInput FirstInput = MakeCompleteGiftInput();
        FirstInput.DiamondCount = 345;
        FirstInput.RepeatCount = 9;
        FirstInput.GiftType = 4;
        FirstInput.bRepeatEnd = true;
        FirstInput.GroupId = "gift-combo-first-group";
        const FTSGiftInput FirstExpected = FirstInput;

        const TTSFamilyDecision<FTSGiftComboPayload> FirstDecision =
            FTSGiftComboFamily::Decide(FirstInput);
        Require(
            FirstDecision.has_value(),
            "First GiftCombo decision must produce a candidate"
        );
        RequireGiftInputEqual(
            FirstInput,
            FirstExpected,
            "First GiftCombo caller input"
        );

        FirstInput.GiftId = 0;
        FirstInput.GiftName = "mutated-first-gift";
        FirstInput.GiftPictureUrl = "mutated-first-picture";
        FirstInput.DiamondCount = 0;
        FirstInput.RepeatCount = 0;
        FirstInput.GiftType = 0;
        FirstInput.Describe = "mutated-first-description";
        FirstInput.bRepeatEnd = false;
        FirstInput.GroupId = "mutated-first-group";
        FirstInput.User.UniqueId = "mutated-first-user";
        FirstInput.User.Nickname = "Mutated First User";
        FirstInput.User.ProfilePictureUrl = "mutated-first-user-picture";
        FirstInput.User.FollowRole = 0;
        FirstInput.User.bIsModerator = false;
        FirstInput.User.bIsSubscriber = true;
        FirstInput.User.bIsNewGifter = false;
        FirstInput.User.TopGifterRank = 0;
        FirstInput.User.GifterLevel = 0;
        FirstInput.User.TeamMemberLevel = 0;

        RequireGiftInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First GiftCombo payload snapshot"
        );

        FTSGiftInput SecondInput = MakeCompleteGiftInput();
        SecondInput.DiamondCount = 901;
        SecondInput.RepeatCount = 31;
        SecondInput.GiftType = 8;
        SecondInput.bRepeatEnd = false;
        SecondInput.GroupId = "gift-combo-second-group";
        SecondInput.User.UniqueId = "gift-combo-second-user";
        SecondInput.User.Nickname = "Second Combo User";
        SecondInput.User.ProfilePictureUrl =
            "https://example.test/second-combo-user.png";
        SecondInput.User.FollowRole = 5;
        SecondInput.User.bIsModerator = false;
        SecondInput.User.bIsSubscriber = true;
        SecondInput.User.bIsNewGifter = false;
        SecondInput.User.TopGifterRank = 21;
        SecondInput.User.GifterLevel = 22;
        SecondInput.User.TeamMemberLevel = 23;
        const FTSGiftInput SecondExpected = SecondInput;

        const TTSFamilyDecision<FTSGiftComboPayload> SecondDecision =
            FTSGiftComboFamily::Decide(SecondInput);
        Require(
            SecondDecision.has_value(),
            "Second GiftCombo decision must produce a candidate"
        );
        Require(
            FirstDecision->FamilyKind == ETSEventFamilyKind::Gift &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Gift &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::GiftCombo &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::GiftCombo &&
                FirstDecision->EnqueueRequest.Flow != ETSEventFlow::Gift &&
                SecondDecision->EnqueueRequest.Flow != ETSEventFlow::Gift,
            "GiftCombo metadata must be stable across independent decisions"
        );

        SecondInput.GiftId = -1;
        SecondInput.GiftName = "mutated-second-gift";
        SecondInput.GiftPictureUrl = "mutated-second-picture";
        SecondInput.DiamondCount = -1;
        SecondInput.RepeatCount = -1;
        SecondInput.GiftType = -1;
        SecondInput.Describe = "mutated-second-description";
        SecondInput.bRepeatEnd = true;
        SecondInput.GroupId = "mutated-second-group";
        SecondInput.User.UniqueId = "mutated-second-user";
        SecondInput.User.Nickname = "Mutated Second User";
        SecondInput.User.ProfilePictureUrl = "mutated-second-user-picture";
        SecondInput.User.FollowRole = -1;
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        SecondInput.User.TopGifterRank = -1;
        SecondInput.User.GifterLevel = -1;
        SecondInput.User.TeamMemberLevel = -1;

        RequireGiftInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second GiftCombo payload snapshot"
        );
        RequireUserEqual(
            SecondDecision->Payload.Input.User,
            SecondExpected.User,
            "Second GiftCombo user snapshot"
        );
        RequireGiftInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "Independent first GiftCombo payload"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftComboPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "GiftCombo produces direct Gift/GiftCombo candidate",
            &TestGiftComboProducesDirectGiftComboCandidate
        });
        Tests.push_back({
            "GiftCombo payload snapshot and metadata independence",
            &TestGiftComboPayloadSnapshotAndMetadataIndependence
        });
    }
}
