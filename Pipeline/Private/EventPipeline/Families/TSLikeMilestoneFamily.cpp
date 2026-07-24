#include "EventPipeline/Families/TSLikeMilestoneFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSLikeMilestonePayload>
FTSLikeMilestoneFamily::Decide(
    FTSLikeInput Input
)
{
    TTSAdmissionCandidate<FTSLikeMilestonePayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Like;
    Candidate.EnqueueRequest.Flow =
        ETSEventFlow::LikeMilestone;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride =
        std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
