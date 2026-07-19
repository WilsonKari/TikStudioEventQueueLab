#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityRoomUserConverter.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonRoomUserReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "roomUser",
  "data": {
    "viewerCount": 100,
    "topGifterRank": 7,
    "topViewers": [
      {
        "coinCount": 1000,
        "user": {
          "uniqueId": "room-viewer-a",
          "nickname": "Room Viewer A",
          "profilePictureUrl": "https://example.test/room-a.png",
          "isModerator": true,
          "isSubscriber": false,
          "gifterLevel": 11,
          "teamMemberLevel": 13
        }
      },
      {
        "coinCount": 500,
        "user": {
          "uniqueId": "room-viewer-b",
          "nickname": "Room Viewer B",
          "profilePictureUrl": "https://example.test/room-b.png",
          "isModerator": false,
          "isSubscriber": true,
          "gifterLevel": 17,
          "teamMemberLevel": 19
        }
      }
    ]
  }
}
)json";

        const FTSTikFinityJsonDecodeResult Decoded =
            FTSTikFinityJsonEventDecoder::Decode(Json);
        Require(
            Decoded.Status == ETSTikFinityJsonDecodeStatus::Decoded &&
                Decoded.Event.has_value(),
            "Vertical RoomUser JSON must decode"
        );

        const FTSTikFinityDecodedRoomUserMessage* RoomUserMessage =
            std::get_if<FTSTikFinityDecodedRoomUserMessage>(&*Decoded.Event);
        Require(
            RoomUserMessage != nullptr,
            "Vertical decoded variant must contain RoomUser"
        );

        const FTSTikFinityRoomUserConversionResult Converted =
            FTSTikFinityRoomUserConverter::Convert(*RoomUserMessage);
        Require(
            Converted.Status ==
                    ETSTikFinityRoomUserConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical RoomUser conversion must produce an input"
        );
        const FTSRoomUserInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostRoomUser(*Converted.Input),
            "Vertical RoomUser publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedRoomUserAdmission(
            AdmissionCycle,
            "Vertical RoomUser admission"
        );
        const FTSRoomUserProcessingDispatch& Dispatch =
            RequireRoomUserDispatch(
                AdmissionCycle,
                "Vertical RoomUser dispatch"
            );
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::RoomUser,
            "Vertical RoomUser dispatch identity and flow"
        );
        RequireRoomUserInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical RoomUser payload snapshot"
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
                ) == nullptr,
            "Vertical RoomUser dispatch must contain no existing family"
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
            "Vertical RoomUser dispatch must be emitted exactly once"
        );

        Require(
            Host.PostRoomUserCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical RoomUser completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::RoomUserCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical RoomUser completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical RoomUser completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical RoomUser completion must leave no dispatch or command"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterRoomUserVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON RoomUser reaches Host dispatch and completion",
            &TestJsonRoomUserReachesHostDispatchAndCompletion
        });
    }
}
