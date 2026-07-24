#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityShareConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonShareExplicitlyReachesShareMilestoneHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "share",
  "data": {
    "uniqueId": "json-share-milestone-user",
    "nickname": "Share Milestone User",
    "profilePictureUrl": "https://example.test/share-milestone.png",
    "followRole": 3,
    "isModerator": true,
    "isSubscriber": false,
    "isNewGifter": true,
    "topGifterRank": 7,
    "gifterLevel": 11,
    "teamMemberLevel": 13
  }
}
)json";

        const FTSTikFinityJsonDecodeResult Decoded =
            FTSTikFinityJsonEventDecoder::Decode(Json);
        Require(
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded &&
                Decoded.Event.has_value(),
            "Vertical ShareMilestone JSON must decode"
        );

        const FTSTikFinityDecodedShareMessage* ShareMessage =
            std::get_if<FTSTikFinityDecodedShareMessage>(&*Decoded.Event);
        Require(
            ShareMessage != nullptr,
            "Vertical decoded variant must contain Share"
        );

        const FTSTikFinityShareConversionResult Converted =
            FTSTikFinityShareConverter::Convert(*ShareMessage);
        Require(
            Converted.Status ==
                    ETSTikFinityShareConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical Share conversion must produce an input"
        );
        const FTSShareInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        // El converter sólo produce FTSShareInput. Esta prueba elige explícitamente
        // el carril; no clasifica Share ni calcula un valor de milestone.
        Require(
            Host.PostShareMilestone(*Converted.Input),
            "Vertical ShareMilestone publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId =
            RequireAcceptedShareMilestoneAdmission(
                AdmissionCycle,
                "Vertical ShareMilestone admission"
            );
        const FTSShareMilestoneProcessingDispatch& Dispatch =
            RequireShareMilestoneDispatch(
                AdmissionCycle,
                "Vertical ShareMilestone dispatch"
            );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::ShareMilestone &&
                std::get_if<FTSShareProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Vertical ShareMilestone dispatch identity and route"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical ShareMilestone payload snapshot"
        );
        Require(
            AdmissionCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::ShareMilestoneInput &&
                !AdmissionCycle.bMoreCommandsPending,
            "Vertical ShareMilestone input command invariant"
        );

        const FTSEventHostCycleResult ProcessingCycle = Host.RunOneCycle();
        Require(
            ProcessingCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::None &&
                !ProcessingCycle.AdmissionResult.has_value() &&
                !ProcessingCycle.CompletionResult.has_value() &&
                !ProcessingCycle.Dispatch.has_value() &&
                ProcessingCycle.PumpResult.has_value() &&
                ProcessingCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy &&
                !ProcessingCycle.bMoreCommandsPending,
            "Vertical ShareMilestone dispatch must be emitted exactly once"
        );

        Require(
            Host.PostShareMilestoneCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical ShareMilestone completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion =
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::ShareMilestoneCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Vertical ShareMilestone completion"
            );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical ShareMilestone completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical ShareMilestone completion must leave no work"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareMilestoneVerticalIntegrationTests(
        FTSTestCases& Tests
    )
    {
        Tests.push_back({
            "JSON Share explicitly reaches ShareMilestone Host dispatch and completion",
            &TestJsonShareExplicitlyReachesShareMilestoneHostDispatchAndCompletion
        });
    }
}
