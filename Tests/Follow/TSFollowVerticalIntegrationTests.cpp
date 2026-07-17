#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityFollowConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonFollowReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "follow",
  "data": {
    "uniqueId": "json-follow-user",
    "nickname": "Follow User",
    "profilePictureUrl": "https://example.test/follow.png",
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
            "Vertical Follow JSON must decode"
        );

        const FTSTikFinityDecodedFollowMessage* FollowMessage =
            std::get_if<FTSTikFinityDecodedFollowMessage>(&*Decoded.Event);
        Require(
            FollowMessage != nullptr,
            "Vertical decoded variant must contain Follow"
        );

        const FTSTikFinityFollowConversionResult Converted =
            FTSTikFinityFollowConverter::Convert(*FollowMessage);
        Require(
            Converted.Status ==
                    ETSTikFinityFollowConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical Follow conversion must produce an input"
        );
        const FTSFollowInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostFollow(*Converted.Input),
            "Vertical Follow publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        Require(
            AdmissionCycle.ProcessedCommand ==
                ETSEventHostCommandKind::FollowInput,
            "Vertical Follow cycle must process the input command"
        );
        const FTSEmissionId EmissionId = RequireAcceptedFollowAdmission(
            AdmissionCycle,
            "Vertical Follow admission"
        );
        const FTSFollowProcessingDispatch& Dispatch = RequireFollowDispatch(
            AdmissionCycle,
            "Vertical Follow dispatch"
        );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Follow,
            "Vertical Follow dispatch identity and flow"
        );
        RequireFollowInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical Follow payload snapshot"
        );

        const FTSEventHostCycleResult ProcessingCycle = Host.RunOneCycle();
        Require(
            ProcessingCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::None &&
                !ProcessingCycle.Dispatch.has_value() &&
                ProcessingCycle.PumpResult.has_value() &&
                ProcessingCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy,
            "Vertical Follow dispatch must be emitted exactly once"
        );

        Require(
            Host.PostFollowCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Follow completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::FollowCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Follow completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Follow completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Follow completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterFollowVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Follow reaches Host dispatch and completion",
            &TestJsonFollowReachesHostDispatchAndCompletion
        });
    }
}
