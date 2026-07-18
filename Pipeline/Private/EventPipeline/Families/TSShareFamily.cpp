#include "EventPipeline/Families/TSShareFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSSharePayload> FTSShareFamily::Decide(
    FTSShareInput Input
)
{
    TTSAdmissionCandidate<FTSSharePayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Share;
    // TikFinity no aporta un contador de hito; este input produce Share directo.
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Share;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;
    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
