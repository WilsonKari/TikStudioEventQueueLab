#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityChatConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonChatReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "chat",
  "data": {
    "comment": "Vertical Chat comment",
    "emotes": [
      {
        "emoteId": "vertical-emote-a",
        "emoteImageUrl": "https://example.test/chat-a.png"
      },
      {
        "emoteId": "vertical-emote-b",
        "emoteImageUrl": "https://example.test/chat-b.png"
      }
    ],
    "uniqueId": "json-chat-user",
    "nickname": "Chat User",
    "profilePictureUrl": "https://example.test/chat-user.png",
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
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded,
            "Vertical Chat JSON decode status"
        );
        Require(
            Decoded.Event.has_value(),
            "Vertical Chat JSON must contain an event"
        );

        const FTSTikFinityDecodedChatMessage* ChatMessage =
            std::get_if<FTSTikFinityDecodedChatMessage>(&*Decoded.Event);
        Require(
            ChatMessage != nullptr,
            "Vertical decoded variant must contain Chat"
        );

        const FTSTikFinityChatConversionResult Converted =
            FTSTikFinityChatConverter::Convert(*ChatMessage);
        Require(
            Converted.Status == ETSTikFinityChatConversionStatus::Converted,
            "Vertical Chat conversion status"
        );
        Require(
            Converted.Input.has_value(),
            "Vertical Chat conversion must produce an input"
        );
        const FTSChatInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostChat(*Converted.Input),
            "Vertical Chat publication must request scheduling"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        Require(
            AdmissionCycle.ProcessedCommand ==
                ETSEventHostCommandKind::ChatInput,
            "Vertical Chat cycle must process the input command"
        );
        const FTSEmissionId EmissionId = RequireAcceptedChatAdmission(
            AdmissionCycle,
            "Vertical Chat admission"
        );
        Require(EmissionId != 0, "Vertical Chat EmissionId must be valid");

        const FTSChatProcessingDispatch& Dispatch = RequireChatDispatch(
            AdmissionCycle,
            "Vertical Chat dispatch"
        );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Chat,
            "Vertical Chat dispatch identity and flow"
        );
        RequireChatPayloadMatchesInput(
            Dispatch.Payload,
            ExpectedInput,
            "Vertical Chat payload snapshot"
        );
        Require(
            Dispatch.Payload.Messages.size() == 1 &&
                !Dispatch.Payload.Messages[0].bIsCommand,
            "Vertical Chat must be classified as one normal semantic message"
        );
        Require(
            std::get_if<FTSFollowProcessingDispatch>(
                &*AdmissionCycle.Dispatch
            ) == nullptr,
            "Vertical Chat dispatch must not contain Follow"
        );

        const FTSEventHostCycleResult ProcessingCycle = Host.RunOneCycle();
        Require(
            ProcessingCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::None &&
                !ProcessingCycle.Dispatch.has_value() &&
                ProcessingCycle.PumpResult.has_value() &&
                ProcessingCycle.PumpResult->Outcome.Status ==
                    ETSPumpStatus::Busy,
            "Vertical Chat dispatch must be emitted exactly once"
        );

        Require(
            Host.PostChatCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Chat completion must request scheduling"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::ChatCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Chat completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Chat completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Chat completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterChatVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Chat reaches Host dispatch and completion",
            &TestJsonChatReachesHostDispatchAndCompletion
        });
    }
}
