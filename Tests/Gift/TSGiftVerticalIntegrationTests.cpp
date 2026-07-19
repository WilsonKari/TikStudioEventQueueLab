#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityGiftConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonGiftReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "gift",
  "data": {
    "giftId": 5655,
    "giftName": "Rose",
    "giftPictureUrl": "https://example.test/gift.png",
    "diamondCount": 20,
    "repeatCount": 7,
    "giftType": 1,
    "describe": "Vertical portable Gift",
    "repeatEnd": true,
    "groupId": "vertical-gift-group",
    "uniqueId": "vertical-gift-user",
    "nickname": "Vertical Gift User",
    "profilePictureUrl": "https://example.test/gift-user.png",
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
            "Vertical Gift JSON must decode"
        );

        const FTSTikFinityDecodedGiftMessage* GiftMessage =
            std::get_if<FTSTikFinityDecodedGiftMessage>(&*Decoded.Event);
        Require(
            GiftMessage != nullptr,
            "Vertical decoded variant must contain Gift"
        );

        const FTSTikFinityGiftConversionResult Converted =
            FTSTikFinityGiftConverter::Convert(*GiftMessage);
        Require(
            Converted.Status == ETSTikFinityGiftConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical Gift conversion must produce an input"
        );
        const FTSGiftInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostGift(*Converted.Input),
            "Vertical Gift publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedGiftAdmission(
            AdmissionCycle,
            "Vertical Gift admission"
        );
        const FTSGiftProcessingDispatch& Dispatch =
            RequireGiftDispatch(AdmissionCycle, "Vertical Gift dispatch");
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Gift,
            "Vertical Gift dispatch identity and flow"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical Gift payload snapshot"
        );
        Require(
            ExpectedInput.RepeatCount > 1 &&
                ExpectedInput.GiftType != 0 &&
                ExpectedInput.bRepeatEnd &&
                !ExpectedInput.GroupId.empty(),
            "Vertical Gift repeat metadata must remain non-trivial"
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
                ) == nullptr &&
                std::get_if<FTSLikeProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr &&
                std::get_if<FTSRoomUserProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Vertical Gift dispatch must contain no other family"
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
            "Vertical Gift dispatch must be emitted exactly once"
        );

        Require(
            Host.PostGiftCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Gift completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::GiftCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Gift completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Gift completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Gift completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Gift reaches Host dispatch and completion",
            &TestJsonGiftReachesHostDispatchAndCompletion
        });
    }
}
