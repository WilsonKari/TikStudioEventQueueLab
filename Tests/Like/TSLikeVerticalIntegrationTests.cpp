#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityLikeConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonLikeReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "like",
  "data": {
    "likeCount": 5,
    "totalLikeCount": 50,
    "uniqueId": "json-like-user",
    "nickname": "Like User",
    "profilePictureUrl": "https://example.test/like.png",
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
            "Vertical Like JSON must decode"
        );

        const FTSTikFinityDecodedLikeMessage* LikeMessage =
            std::get_if<FTSTikFinityDecodedLikeMessage>(&*Decoded.Event);
        Require(
            LikeMessage != nullptr,
            "Vertical decoded variant must contain Like"
        );

        const FTSTikFinityLikeConversionResult Converted =
            FTSTikFinityLikeConverter::Convert(*LikeMessage);
        Require(
            Converted.Status ==
                    ETSTikFinityLikeConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical Like conversion must produce an input"
        );
        const FTSLikeInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostLike(*Converted.Input),
            "Vertical Like publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedLikeAdmission(
            AdmissionCycle,
            "Vertical Like admission"
        );
        const FTSLikeProcessingDispatch& Dispatch = RequireLikeDispatch(
            AdmissionCycle,
            "Vertical Like dispatch"
        );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Like,
            "Vertical Like dispatch identity and flow"
        );
        RequireLikeInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical Like payload snapshot"
        );
        Require(
            std::get_if<FTSChatProcessingDispatch>(
                &*AdmissionCycle.Dispatch
            ) == nullptr &&
                std::get_if<FTSFollowProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSShareProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Vertical Like dispatch must contain no existing family"
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
            "Vertical Like dispatch must be emitted exactly once"
        );

        Require(
            Host.PostLikeCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Like completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::LikeCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Like completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Like completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Like completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikeVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Like reaches Host dispatch and completion",
            &TestJsonLikeReachesHostDispatchAndCompletion
        });
    }
}
