#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityShareConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonShareReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "share",
  "data": {
    "uniqueId": "json-share-user",
    "nickname": "Share User",
    "profilePictureUrl": "https://example.test/share.png",
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
            "Vertical Share JSON must decode"
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
        Require(
            Host.PostShare(*Converted.Input),
            "Vertical Share publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedShareAdmission(
            AdmissionCycle,
            "Vertical Share admission"
        );
        const FTSShareProcessingDispatch& Dispatch = RequireShareDispatch(
            AdmissionCycle,
            "Vertical Share dispatch"
        );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Share,
            "Vertical Share dispatch identity and flow"
        );
        RequireShareInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical Share payload snapshot"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(
                &*AdmissionCycle.Dispatch
            ) == nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Vertical Share dispatch must contain neither Chat nor Follow"
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
                    ETSPumpStatus::Busy,
            "Vertical Share dispatch must be emitted exactly once"
        );

        Require(
            Host.PostShareCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Share completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ShareCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Share completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Share completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Share completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Share reaches Host dispatch and completion",
            &TestJsonShareReachesHostDispatchAndCompletion
        });
    }
}
