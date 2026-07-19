#include "EventPipeline/Families/TSGiftFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSGiftPayload> FTSGiftFamily::Decide(
    FTSGiftInput Input
)
{
    TTSAdmissionCandidate<FTSGiftPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Gift;

    // GiftCombo requiere estado y reglas que Gift A no define.
    Candidate.EnqueueRequest.Flow = ETSEventFlow::Gift;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
