#include "EventHost/TSEventExecutionHost.h"
#include "TikFinity/TSTikFinityJsonEventDecoder.h"
#include "TikFinity/TSTikFinityMemberConverter.h"
#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    void TestJsonMemberReachesHostDispatchAndCompletion()
    {
        constexpr const char* Json = R"json(
{
  "event": "member",
  "data": {
    "actionId": 73,
    "uniqueId": "vertical-member-user",
    "nickname": "Vertical Member User",
    "profilePictureUrl": "https://example.test/member-user.png",
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
            "Vertical Member JSON must decode"
        );

        const FTSTikFinityDecodedMemberMessage* MemberMessage =
            std::get_if<FTSTikFinityDecodedMemberMessage>(&*Decoded.Event);
        Require(
            MemberMessage != nullptr,
            "Vertical decoded variant must contain Member"
        );

        const FTSTikFinityMemberConversionResult Converted =
            FTSTikFinityMemberConverter::Convert(*MemberMessage);
        Require(
            Converted.Status ==
                    ETSTikFinityMemberConversionStatus::Converted &&
                Converted.Input.has_value(),
            "Vertical Member conversion must produce an input"
        );
        const FTSMemberInput ExpectedInput = *Converted.Input;

        FTSEventExecutionHost Host;
        Require(
            Host.PostMember(*Converted.Input),
            "Vertical Member publication must signal"
        );

        const FTSEventHostCycleResult AdmissionCycle = Host.RunOneCycle();
        const FTSEmissionId EmissionId = RequireAcceptedMemberAdmission(
            AdmissionCycle,
            "Vertical Member admission"
        );
        const FTSMemberProcessingDispatch& Dispatch =
            RequireMemberDispatch(AdmissionCycle, "Vertical Member dispatch");
        Require(
            Dispatch.Emission.EmissionId == EmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::MemberIdentity &&
                Dispatch.Emission.Flow != ETSEventFlow::MemberNormalized,
            "Vertical Member dispatch identity and direct flow"
        );
        RequireMemberInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            "Vertical Member payload snapshot"
        );
        Require(
            ExpectedInput.ActionId == 73 &&
                ExpectedInput.User.UniqueId == "vertical-member-user",
            "Vertical Member data must remain non-trivial"
        );
        Require(
            std::get_if<FTSMemberProcessingDispatch>(
                &*AdmissionCycle.Dispatch
            ) != nullptr &&
                std::get_if<FTSGiftProcessingDispatch>(
                    &*AdmissionCycle.Dispatch
                ) == nullptr,
            "Vertical Host variant must contain only Member"
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
            "Vertical Member dispatch must be emitted exactly once"
        );

        Require(
            Host.PostMemberCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            ),
            "Vertical Member completion must signal"
        );
        const FTSEventHostCycleResult CompletionCycle = Host.RunOneCycle();
        const FTSProcessingCompletionResult& Completion = RequireCompletion(
            CompletionCycle,
            ETSEventHostCommandKind::MemberCompletion,
            EmissionId,
            ETSProcessingResult::Succeeded,
            "Vertical Member completion"
        );
        RequireConfirmedCompletion(
            Completion,
            EmissionId,
            "Vertical Member completion"
        );
        Require(
            !CompletionCycle.Dispatch.has_value() &&
                !CompletionCycle.bMoreCommandsPending,
            "Vertical Member completion must leave no work"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterMemberVerticalIntegrationTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "JSON Member reaches Host MemberIdentity dispatch and completion",
            &TestJsonMemberReachesHostDispatchAndCompletion
        });
    }
}
