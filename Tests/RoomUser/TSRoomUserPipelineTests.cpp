#include "EventPipeline/Families/TSRoomUserFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <utility>

namespace
{
    using namespace TikStudio::Tests;

    void TestRoomUserCandidateAndAdmissionDefaults()
    {
        const FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        const TTSFamilyDecision<FTSRoomUserPayload> Decision =
            FTSRoomUserFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "RoomUser must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSRoomUserPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::RoomUser,
            "RoomUser candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::RoomUser,
            "RoomUser candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow !=
                    ETSEventFlow::RoomUserMilestone &&
                Candidate.EnqueueRequest.Flow !=
                    ETSEventFlow::RoomUserTop1Change,
            "RoomUser A must not select a derived flow"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "RoomUser must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "RoomUser candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "RoomUser candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "RoomUser candidate must not request eviction protection"
        );
        RequireRoomUserInputEqual(
            Candidate.Payload.Input,
            Input,
            "RoomUser candidate payload"
        );
    }

    void TestRoomUserPayloadSnapshotAndInputPreservation()
    {
        FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        const FTSRoomUserInput Original = Input;

        const TTSFamilyDecision<FTSRoomUserPayload> Decision =
            FTSRoomUserFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "RoomUser snapshot must produce a candidate"
        );
        RequireRoomUserInputEqual(
            Input,
            Original,
            "RoomUser input after by-copy decision"
        );

        Input.ViewerCount = 0;
        Input.TopGifterRank = 0;
        Input.TopViewers[0].UniqueId = "mutated-room-viewer";
        Input.TopViewers[0].Nickname.clear();
        Input.TopViewers[0].ProfilePictureUrl.clear();
        Input.TopViewers[0].CoinCount = 0;
        Input.TopViewers[0].bIsModerator = false;
        Input.TopViewers[0].bIsSubscriber = true;
        Input.TopViewers[0].GifterLevel = 0;
        Input.TopViewers[0].TeamMemberLevel = 0;
        std::swap(Input.TopViewers[0], Input.TopViewers[1]);
        Input.TopViewers.pop_back();

        RequireRoomUserInputEqual(
            Decision->Payload.Input,
            Original,
            "RoomUser payload snapshot"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterRoomUserPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "RoomUser candidate and admission defaults",
            &TestRoomUserCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "RoomUser payload snapshot and input preservation",
            &TestRoomUserPayloadSnapshotAndInputPreservation
        });
    }
}
