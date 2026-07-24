#include "EventPipeline/Families/TSShareMilestoneFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSShareMilestonePayload>
FTSShareMilestoneFamily::Decide(
    FTSShareInput Input
)
{
    TTSAdmissionCandidate<FTSShareMilestonePayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Share;
    Candidate.EnqueueRequest.Flow =
        ETSEventFlow::ShareMilestone;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride =
        std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
