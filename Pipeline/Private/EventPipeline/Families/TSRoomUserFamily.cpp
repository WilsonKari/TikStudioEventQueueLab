#include "EventPipeline/Families/TSRoomUserFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSRoomUserPayload> FTSRoomUserFamily::Decide(
    FTSRoomUserInput Input
)
{
    TTSAdmissionCandidate<FTSRoomUserPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::RoomUser;

    // Los flujos derivados requieren estado y reglas que RoomUser A no define.
    Candidate.EnqueueRequest.Flow = ETSEventFlow::RoomUser;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
