#pragma once

#include "EventPipeline/Payloads/TSChatPayload.h"
#include "EventPipeline/Settings/TSEventPipelineSettings.h"
#include "EventPipeline/TikStudioEventPipelineContracts.h"

#include <cstdint>

enum class ETSChatFamilyDecisionStatus : std::uint8_t
{
    NoEmission,
    RejectedInvalidInput,
    RejectedSemanticLimit,
    Candidate
};

struct FTSChatFamilyDecision
{
    ETSChatFamilyDecisionStatus Status =
        ETSChatFamilyDecisionStatus::NoEmission;
    TTSFamilyDecision<FTSChatPayload> Candidate;
};

class FTSChatFamily final
{
public:
    [[nodiscard]]
    static FTSChatFamilyDecision Decide(
        FTSChatInput Input,
        FTSEventQueueTimePoint ReceivedAt,
        const FTSEventPipelineSettings& Settings
    );

    [[nodiscard]]
    static bool IsCommand(
        const std::string& Comment,
        const FTSChatSemanticSettings& Settings
    ) noexcept;

    [[nodiscard]]
    static bool IsWithinBatchLimits(
        const FTSChatPayload& Payload,
        const FTSChatSemanticSettings& Settings
    ) noexcept;
};
