#include "EventPipeline/Families/TSFollowFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSFollowPayload> FTSFollowFamily::Decide(
    FTSFollowInput Input
)
{
    TTSAdmissionCandidate<FTSFollowPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Follow;
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Follow;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;
    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
