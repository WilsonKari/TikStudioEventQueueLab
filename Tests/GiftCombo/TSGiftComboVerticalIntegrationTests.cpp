#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityGiftConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonGiftExplicitlyReachesGiftComboHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "gift",
  "data": {
    "giftId": 8800,
    "giftName": "Explicit Combo Rose",
    "giftPictureUrl": "https://example.test/combo-gift.png",
    "diamondCount": 250,
    "repeatCount": 12,
    "giftType": 3,
    "describe": "Explicit vertical GiftCombo payload",
    "repeatEnd": true,
    "groupId": "vertical-gift-combo-group",
    "uniqueId": "vertical-gift-combo-user",
    "nickname": "Vertical GiftCombo User",
    "profilePictureUrl": "https://example.test/combo-user.png",
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
            "Explicit GiftCombo JSON must decode as Gift"
        );

        const FTSTikFinityDecodedGiftMessage* GiftMessage =
            std::get_if<FTSTikFinityDecodedGiftMessage>(&*Decoded.Event);
        Require(
            GiftMessage != nullptr,
            "Explicit GiftCombo decoded variant must contain Gift"
        );

        const FTSTikFinityGiftConversionResult Converted =
            FTSTikFinityGiftConverter::Convert(*GiftMessage);
        Require(
            Converted.Status == ETSTikFinityGiftConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Explicit GiftCombo conversion must produce FTSGiftInput"
        );
        const FTSGiftInput ExpectedInput = *Converted.Input;
        Require(
            ExpectedInput.RepeatCount > 1 &&
                ExpectedInput.GiftType != 0 &&
                ExpectedInput.bRepeatEnd &&
                !ExpectedInput.GroupId.empty(),
            "Explicit GiftCombo metadata must remain non-trivial"
        );

        FTSEventExecutionHost Host;
        // El test elige el carril explícitamente; el converter no clasifica GiftCombo.
        Require(
            Host.PostGiftCombo(*Converted.Input),
            "Explicit GiftCombo publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedGiftComboAdmission(
            AdmissionCycle,
            "Explicit GiftCombo admission"
        );
        const FTSGiftComboProcessingDispatch& Dispatch =
            RequireGiftComboDispatch(
                AdmissionCycle,
                "Explicit GiftCombo dispatch"
            );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::GiftCombo &&
                std::get_if<FTSGiftProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Explicit GiftCombo must not use direct Gift dispatch"
        );
        RequireGiftInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Explicit GiftCombo payload snapshot"
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
            "Explicit GiftCombo dispatch must be emitted exactly once"
        );

        Require(
            Host.PostGiftComboCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Explicit GiftCombo completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        RequireConfirmedCompletion(
            RequireCompletion(
                CompletionCycle,
                ETSEventHostCommandKind::GiftComboCompletion,
                EmissionId,
                ETSProcessingResult::Succeeded,
                "Explicit GiftCombo completion"
            ),
            EmissionId,
            "Explicit GiftCombo completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Explicit GiftCombo completion must leave no work"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftComboVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Gift explicitly reaches GiftCombo Host dispatch and completion",
            &TestJsonGiftExplicitlyReachesGiftComboHostDispatchAndCompletion
        });
    }
}
