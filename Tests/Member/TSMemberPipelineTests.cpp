#include "EventPipeline/Families/TSMemberFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
    using namespace TikStudio::Tests;

    [[nodiscard]]
    FTSMemberInput MakeCompleteMemberInput(
        std::int32_t ActionId,
        const std::string& Label
    )
    {
        FTSMemberInput Input;
        Input.ActionId = ActionId;
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl =
            "https://example.test/member-user.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    void RequireMemberInputEqual(
        const FTSMemberInput& Actual,
        const FTSMemberInput& Expected,
        const std::string& Context
    )
    {
        Require(Actual.ActionId == Expected.ActionId, Context + ": ActionId");
        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    void RequireDirectMemberIdentityCandidate(
        const TTSFamilyDecision<FTSMemberPayload>& Decision,
        const FTSMemberInput& ExpectedInput,
        const std::string& Context
    )
    {
        Require(Decision.has_value(), Context + ": candidate expected");
        const TTSAdmissionCandidate<FTSMemberPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Member,
            Context + ": FamilyKind"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::MemberIdentity,
            Context + ": direct flow"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            Context + ": PriorityAdjustment"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL &&
                Candidate.EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0},
            Context + ": TTL defaults"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            Context + ": eviction protection"
        );
        RequireMemberInputEqual(
            Candidate.Payload.Input,
            ExpectedInput,
            Context + ": payload"
        );
    }

    void TestMemberProducesDirectMemberIdentityCandidate()
    {
        const FTSMemberInput Input = MakeCompleteMemberInput(73, "direct");
        const TTSFamilyDecision<FTSMemberPayload> Decision =
            FTSMemberFamily::Decide(Input);

        RequireDirectMemberIdentityCandidate(
            Decision,
            Input,
            "Member direct candidate"
        );
    }

    void TestMemberMetadataDoesNotActivateMemberNormalized()
    {
        const FTSMemberInput First = MakeCompleteMemberInput(0, "first");
        const FTSMemberInput Second = MakeCompleteMemberInput(
            std::numeric_limits<std::int32_t>::max(),
            "second"
        );

        const TTSFamilyDecision<FTSMemberPayload> FirstDecision =
            FTSMemberFamily::Decide(First);
        const TTSFamilyDecision<FTSMemberPayload> SecondDecision =
            FTSMemberFamily::Decide(Second);

        RequireDirectMemberIdentityCandidate(
            FirstDecision,
            First,
            "First independent Member decision"
        );
        RequireDirectMemberIdentityCandidate(
            SecondDecision,
            Second,
            "Second independent Member decision"
        );
        Require(
            FirstDecision->EnqueueRequest.Flow !=
                    ETSEventFlow::MemberNormalized &&
                SecondDecision->EnqueueRequest.Flow !=
                    ETSEventFlow::MemberNormalized,
            "Member metadata must not select MemberNormalized"
        );
        RequireMemberInputEqual(
            FirstDecision->Payload.Input,
            First,
            "First decision remains independent"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterMemberPipelineTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Member produces direct MemberIdentity candidate",
            &TestMemberProducesDirectMemberIdentityCandidate
        });
        Tests.push_back({
            "Member metadata does not activate MemberNormalized",
            &TestMemberMetadataDoesNotActivateMemberNormalized
        });
    }
}
