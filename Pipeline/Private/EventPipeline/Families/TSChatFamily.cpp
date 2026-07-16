#include "EventPipeline/Families/TSChatFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSChatPayload> FTSChatFamily::Decide(
    FTSChatInput Input
)
{
    TTSAdmissionCandidate<FTSChatPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Chat;
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Chat;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;
    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
