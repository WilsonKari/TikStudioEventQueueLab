#include "TSHostTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <variant>

namespace
{
    using namespace TikStudio::Tests;

    struct FFailingHostClock
    {
        FTSEventQueueTimePoint Now{};
        std::size_t CaptureCount = 0;
        std::size_t ThrowOnCapture = 0;

        [[nodiscard]]
        FTSNowProvider MakeProvider()
        {
            return [this]()
            {
                ++CaptureCount;
                if (CaptureCount == ThrowOnCapture)
                {
                    throw std::runtime_error("Controlled Host clock failure");
                }
                return Now;
            };
        }
    };

    void TestHostAppliesFlowSettingsUpdateInGlobalFIFOOrder()
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            10,
            std::chrono::milliseconds{0},
            false,
            false
        );
        const FTSFlowQueueSettings* InitialChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(
            InitialChatSettings != nullptr,
            "Host FIFO Chat settings must exist"
        );

        FTSFlowQueueSettings DisabledSettings = *InitialChatSettings;
        DisabledSettings.bEnabled = false;

        FTSEventExecutionHost Host(Settings);
        Require(
            Host.PostChat(MakeChatInput("before-update")),
            "First FIFO post must transition the tray to non-empty"
        );
        Require(
            !Host.PostFlowSettingsUpdate(
                ETSEventFlow::Chat,
                DisabledSettings
            ),
            "Queued settings update must preserve the occupied tray state"
        );
        Require(
            !Host.PostChat(MakeChatInput("after-update")),
            "Second Chat must remain behind the settings update"
        );

        const FTSEventHostCycleResult BeforeUpdate = Host.RunOneCycle();
        Require(
            BeforeUpdate.ProcessedCommand ==
                    ETSEventHostCommandKind::ChatInput &&
                BeforeUpdate.AdmissionResult.has_value() &&
                BeforeUpdate.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::Accepted &&
                BeforeUpdate.Dispatch.has_value(),
            "Chat before settings update must use the original enabled settings"
        );
        Require(
            !BeforeUpdate.FlowSettingsUpdateResult.has_value() &&
                BeforeUpdate.bMoreCommandsPending,
            "First FIFO cycle must leave the update and later Chat queued"
        );

        const FTSEventHostCycleResult UpdateCycle = Host.RunOneCycle();
        Require(
            UpdateCycle.ProcessedCommand ==
                    ETSEventHostCommandKind::FlowSettingsUpdate &&
                UpdateCycle.FlowSettingsUpdateResult.has_value() &&
                UpdateCycle.FlowSettingsUpdateResult->Status ==
                    ETSUpdateFlowSettingsStatus::Updated &&
                UpdateCycle.FlowSettingsUpdateResult->Flow ==
                    ETSEventFlow::Chat,
            "FIFO settings command must report the Core update result"
        );
        Require(
            !UpdateCycle.AdmissionResult.has_value() &&
                !UpdateCycle.CompletionResult.has_value() &&
                !UpdateCycle.PumpResult.has_value() &&
                !UpdateCycle.Dispatch.has_value() &&
                UpdateCycle.bMoreCommandsPending,
            "Settings command must not admit, complete, Pump or dispatch"
        );

        const FTSEventHostCycleResult AfterUpdate = Host.RunOneCycle();
        Require(
            AfterUpdate.ProcessedCommand ==
                    ETSEventHostCommandKind::ChatInput &&
                AfterUpdate.AdmissionResult.has_value() &&
                AfterUpdate.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                AfterUpdate.AdmissionResult->EnqueueResult.has_value() &&
                AfterUpdate.AdmissionResult->EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Chat after settings update must use the disabled settings"
        );
        Require(
            !AfterUpdate.FlowSettingsUpdateResult.has_value() &&
                !AfterUpdate.bMoreCommandsPending,
            "Final FIFO cycle must consume only the later Chat"
        );
    }

    void TestHostReportsRejectedFlowSettingsUpdate()
    {
        FTSEventQueueSettings Settings = MakeChatSettings(
            10,
            std::chrono::milliseconds{0},
            false,
            false
        );
        const FTSFlowQueueSettings* InitialChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(
            InitialChatSettings != nullptr,
            "Host rejection Chat settings must exist"
        );

        FTSFlowQueueSettings InvalidSettings = *InitialChatSettings;
        InvalidSettings.TTL = std::chrono::milliseconds{-1};

        FTSEventExecutionHost Host(Settings);
        Require(
            Host.PostFlowSettingsUpdate(
                ETSEventFlow::Chat,
                std::move(InvalidSettings)
            ),
            "Invalid settings command must be accepted into an empty tray"
        );

        const FTSEventHostCycleResult Rejected = Host.RunOneCycle();
        Require(
            Rejected.ProcessedCommand ==
                    ETSEventHostCommandKind::FlowSettingsUpdate &&
                Rejected.FlowSettingsUpdateResult.has_value() &&
                Rejected.FlowSettingsUpdateResult->Status ==
                    ETSUpdateFlowSettingsStatus::RejectedInvalidTTL &&
                Rejected.FlowSettingsUpdateResult->Flow ==
                    ETSEventFlow::Chat,
            "Host must expose the rejected settings result"
        );
        Require(
            !Rejected.AdmissionResult.has_value() &&
                !Rejected.CompletionResult.has_value() &&
                !Rejected.PumpResult.has_value() &&
                !Rejected.Dispatch.has_value() &&
                !Rejected.bMoreCommandsPending,
            "Rejected settings command must not produce other command results"
        );

        Require(
            Host.PostChat(MakeChatInput("after-rejected-update")),
            "Host must continue accepting work after a rejected update"
        );
        const FTSEventHostCycleResult Admission = Host.RunOneCycle();
        Require(
            Admission.ProcessedCommand ==
                    ETSEventHostCommandKind::ChatInput &&
                Admission.AdmissionResult.has_value() &&
                Admission.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::Accepted,
            "Rejected settings update must leave the original settings active"
        );
    }

    void TestHostRetainsFailedFrontCommandAndPreservesFIFO()
    {
        FFailingHostClock Clock;
        Clock.ThrowOnCapture = 2;
        FTSEventExecutionHost Host(
            MakeChatSettings(
                10,
                std::chrono::milliseconds{0},
                false,
                false
            ),
            Clock.MakeProvider()
        );

        Require(
            Host.PostChat(MakeChatInput("leased-front")),
            "Failed command setup must signal the empty tray"
        );
        Require(
            !Host.PostChat(MakeChatInput("queued-after-front")),
            "Second command must remain behind the leased front"
        );

        bool bThrew = false;
        try
        {
            (void)Host.RunOneCycle();
        }
        catch (const std::runtime_error&)
        {
            bThrew = true;
        }
        Require(bThrew, "Command processing failure must propagate");

        Clock.ThrowOnCapture = 0;
        const FTSEventHostCycleResult Retried = Host.RunOneCycle();
        Require(
            Retried.ProcessedCommand == ETSEventHostCommandKind::ChatInput &&
                Retried.AdmissionResult.has_value() &&
                Retried.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::Accepted &&
                Retried.AdmissionResult->EnqueueResult.has_value() &&
                Retried.AdmissionResult->EnqueueResult->AdmittedEmission.EmissionId ==
                    1 &&
                Retried.Dispatch.has_value() &&
                Retried.bMoreCommandsPending,
            "The failed front command must retry once before later work"
        );
        const FTSChatProcessingDispatch* FirstDispatch =
            std::get_if<FTSChatProcessingDispatch>(&*Retried.Dispatch);
        Require(
            FirstDispatch != nullptr &&
                FirstDispatch->Payload.Messages.size() == 1 &&
                FirstDispatch->Payload.Messages[0].Comment == "leased-front",
            "Retried dispatch must belong to the original front command"
        );

        const FTSEventHostCycleResult Later = Host.RunOneCycle();
        Require(
            Later.ProcessedCommand == ETSEventHostCommandKind::ChatInput &&
                Later.AdmissionResult.has_value() &&
                Later.AdmissionResult->Status ==
                    ETSPipelineAdmissionStatus::Accepted &&
                Later.AdmissionResult->EnqueueResult.has_value() &&
                Later.AdmissionResult->EnqueueResult->AdmittedEmission.EmissionId ==
                    2 &&
                !Later.bMoreCommandsPending,
            "The later command must keep FIFO and execute after the retry"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterHostInfrastructureTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Host applies flow settings update in FIFO order",
            &TestHostAppliesFlowSettingsUpdateInGlobalFIFOOrder
        });
        Tests.push_back({
            "Host reports rejected flow settings update",
            &TestHostReportsRejectedFlowSettingsUpdate
        });
        Tests.push_back({
            "Host retains failed front command and preserves FIFO",
            &TestHostRetainsFailedFrontCommandAndPreservesFIFO
        });
    }
}
