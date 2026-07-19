#include "EventPipeline/Families/TSMemberFamily.h"

#include <chrono>
#include <utility>

TTSFamilyDecision<FTSMemberPayload> FTSMemberFamily::Decide(
    FTSMemberInput Input
)
{
    TTSAdmissionCandidate<FTSMemberPayload> Candidate;
    Candidate.FamilyKind = ETSEventFamilyKind::Member;
    Candidate.EnqueueRequest.Flow = ETSEventFlow::MemberIdentity;
    Candidate.EnqueueRequest.PriorityAdjustment = 0;
    Candidate.EnqueueRequest.bOverrideTTL = false;
    Candidate.EnqueueRequest.TTLOverride = std::chrono::milliseconds{0};
    Candidate.EnqueueRequest.bProtectedFromEviction = false;
    Candidate.Payload.Input = std::move(Input);
    return Candidate;
}
