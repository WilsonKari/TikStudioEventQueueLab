#include "EventPipeline/Families/TSGiftComboFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSGiftComboPayload>
FTSGiftComboFamily::Decide(
    FTSGiftInput Input
)
{
    TTSAdmissionCandidate<FTSGiftComboPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Gift;
    Candidate.EnqueueRequest.Flow = ETSEventFlow::GiftCombo;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride =
        std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;

    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
