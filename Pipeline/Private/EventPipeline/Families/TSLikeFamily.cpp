#include "EventPipeline/Families/TSLikeFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSLikePayload> FTSLikeFamily::Decide(
    FTSLikeInput Input
)
{
    TTSAdmissionCandidate<FTSLikePayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Like;

    // Los contadores se conservan como datos; LikeUser aún no tiene semántica.
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Like;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
