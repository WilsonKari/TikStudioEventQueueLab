#include "EventPipeline/Families/TSChatFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestChatCandidateAndAdmissionDefaults()
    {
        FTSChatInput Input;
        Input.Comment = "Hello";

        const TTSFamilyDecision<FTSChatPayload> Decision =
            FTSChatFamily::Decide(Input);

        Require(Decision.has_value(), "Chat must produce an admission candidate");

        const TTSAdmissionCandidate<FTSChatPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Chat,
            "Chat candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Chat,
            "Chat candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Chat candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Chat candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride.count() == 0,
            "Chat candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Chat candidate must not request special eviction protection"
        );
    }

    void TestChatPayloadSnapshotAndInputPreservation()
    {
        FTSChatInput Input = MakeCompleteInput();
        const FTSChatInput Original = Input;

        const TTSFamilyDecision<FTSChatPayload> Decision =
            FTSChatFamily::Decide(Input);

        Require(Decision.has_value(), "Chat snapshot must produce a candidate");
        RequireChatInputEqual(
            Decision->Payload.Input,
            Original,
            "Chat payload snapshot"
        );
        RequireChatInputEqual(
            Input,
            Original,
            "Chat input after by-copy decision"
        );
    }

    void TestChatCoordinatorFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();

        const FTSPipelineAdmissionResult Result = Coordinator.SubmitChat(Input);
        Require(
            Result.Status == ETSPipelineAdmissionStatus::Accepted,
            "First Chat admission must be accepted by the coordinator"
        );
        Require(Result.EnqueueResult.has_value(), "Accepted admission must expose core result");

        const FTSEnqueueResult& CoreResult = *Result.EnqueueResult;
        Require(
            CoreResult.Status == ETSEnqueueStatus::Accepted,
            "First Chat admission must be accepted by the core"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status == ETSPumpStatus::EmissionReady,
            "First Chat admission must Auto Pump while idle"
        );
        Require(
            CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId
                == CoreResult.AdmittedEmission.EmissionId,
            "Auto Pump ready identity must match the admitted identity"
        );

        bool bVisitedBinding = false;
        Require(
            Coordinator.VisitEmissionBinding(
                CoreResult.AdmittedEmission.EmissionId,
                [&](const FTSEmissionBinding& Binding)
                {
                    bVisitedBinding = true;
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Chat,
                        "Accepted binding must belong to Chat"
                    );
                    Require(
                        Binding.ExpectedFlow == ETSEventFlow::Chat,
                        "Accepted binding must expect the Chat flow"
                    );
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Accepted binding must start Bound"
                    );
                    Require(
                        Binding.PayloadHandle.Value != 0,
                        "Accepted binding must reference a valid payload handle"
                    );
                }
            ),
            "Accepted Chat binding must be visitable"
        );
        Require(bVisitedBinding, "Binding callback must run for an accepted Chat");

        bool bVisitedPayload = false;
        Require(
            Coordinator.VisitChatPayloadForEmission(
                CoreResult.AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    bVisitedPayload = true;
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "First coordinated Chat payload"
                    );
                }
            ),
            "Accepted Chat payload must be resolvable by EmissionId"
        );
        Require(bVisitedPayload, "Payload callback must run for an accepted Chat");
        Require(Coordinator.GetBindingCount() == 1, "First admission must create one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "First admission must retain one Chat payload"
        );
    }

    void TestChatCoordinatorSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "First coordinated Chat";
        FTSChatInput SecondInput = MakeCompleteInput();
        SecondInput.Comment = "Second coordinated Chat";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitChat(SecondInput);

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First busy-path setup admission must succeed"
        );
        Require(
            Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Second Chat must still be admitted while the core is busy"
        );
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "First Chat must become InFlight"
        );
        Require(
            Second.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::NotRequested,
            "Second busy admission must report Auto Pump NotRequested"
        );
        Require(
            First.EnqueueResult->AdmittedEmission.EmissionId
                != Second.EnqueueResult->AdmittedEmission.EmissionId,
            "Two coordinated admissions must receive distinct identities"
        );
        Require(Coordinator.GetBindingCount() == 2, "Busy admission must retain two bindings");
        Require(
            Coordinator.GetChatPayloadCount() == 2,
            "Busy admission must retain two Chat payloads"
        );

        Require(
            Coordinator.VisitChatPayloadForEmission(
                First.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        FirstInput,
                        "First busy-path payload"
                    );
                }
            ),
            "First busy-path payload must remain resolvable"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                Second.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        SecondInput,
                        "Second busy-path payload"
                    );
                }
            ),
            "Second busy-path payload must be resolvable"
        );
    }

    void TestChatCoordinatorRejectsDisabledFlow()
    {
        FTSEventPipelineCoordinator Coordinator(MakeChatSettings(false, 30));

        const FTSPipelineAdmissionResult Result =
            Coordinator.SubmitChat(MakeCompleteInput());

        Require(
            Result.Status == ETSPipelineAdmissionStatus::RejectedByCore,
            "Disabled Chat must be classified as rejected by core"
        );
        Require(Result.EnqueueResult.has_value(), "Core rejection must retain its result");
        Require(
            Result.EnqueueResult->Status == ETSEnqueueStatus::RejectedDisabled,
            "Disabled Chat must preserve RejectedDisabled"
        );
        Require(Coordinator.GetBindingCount() == 0, "Disabled Chat must not create a binding");
        Require(
            Coordinator.GetChatPayloadCount() == 0,
            "Disabled Chat must roll back its provisional payload"
        );
    }

    void TestChatCoordinatorCapacityRejectionPreservesPriorAdmission()
    {
        FTSEventPipelineCoordinator Coordinator(MakeChatSettings(true, 1));
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "Capacity owner";
        FTSChatInput RejectedInput = MakeCompleteInput();
        RejectedInput.Comment = "Rejected at capacity";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity admission must succeed"
        );

        const FTSEmissionId FirstEmissionId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitChat(RejectedInput);

        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore,
            "Second capacity admission must be rejected by core"
        );
        Require(Rejected.EnqueueResult.has_value(), "Capacity rejection must retain core result");
        Require(
            Rejected.EnqueueResult->Status == ETSEnqueueStatus::RejectedAtCapacity,
            "Second capacity admission must preserve RejectedAtCapacity"
        );
        Require(Coordinator.GetBindingCount() == 1, "Capacity rejection must keep one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "Capacity rejection must remove only its provisional payload"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstEmissionId,
                [](const FTSEmissionBinding&) {}
            ),
            "Capacity rejection must preserve the first binding"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                FirstEmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Capacity-preserved payload"
                    );
                }
            ),
            "Capacity rejection must preserve the first payload"
        );
    }

    void TestChatCoordinatorUnknownEmissionInspection()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSPipelineAdmissionResult Accepted =
            Coordinator.SubmitChat(MakeCompleteInput());
        Require(
            Accepted.Status == ETSPipelineAdmissionStatus::Accepted,
            "Unknown inspection setup admission must succeed"
        );

        const std::size_t BindingCount = Coordinator.GetBindingCount();
        const std::size_t PayloadCount = Coordinator.GetChatPayloadCount();
        bool bBindingCallbackCalled = false;
        bool bPayloadCallbackCalled = false;

        Require(
            !Coordinator.VisitEmissionBinding(
                9999,
                [&](const FTSEmissionBinding&)
                {
                    bBindingCallbackCalled = true;
                }
            ),
            "Unknown binding inspection must return false"
        );
        Require(
            !Coordinator.VisitChatPayloadForEmission(
                9999,
                [&](const FTSChatPayload&)
                {
                    bPayloadCallbackCalled = true;
                }
            ),
            "Unknown payload inspection must return false"
        );
        Require(!bBindingCallbackCalled, "Unknown binding must not invoke its callback");
        Require(!bPayloadCallbackCalled, "Unknown payload must not invoke its callback");
        Require(
            Coordinator.GetBindingCount() == BindingCount,
            "Unknown inspection must not change binding count"
        );
        Require(
            Coordinator.GetChatPayloadCount() == PayloadCount,
            "Unknown inspection must not change payload count"
        );
    }

    void TestChatDispatchWithoutPendingReady()
    {
        FTSEventPipelineCoordinator Coordinator;

        const FTSChatDispatchResult Result =
            Coordinator.BeginChatProcessing();

        Require(
            Result.Status == ETSPipelineDispatchStatus::NoEmissionReady,
            "A new coordinator must not have an authorized ready"
        );
        Require(!Result.Dispatch.has_value(), "No ready must not produce a dispatch");
        Require(Coordinator.GetBindingCount() == 0, "No ready must not create bindings");
        Require(
            Coordinator.GetChatPayloadCount() == 0,
            "No ready must not create Chat payloads"
        );
    }

    void TestAuthorizedChatReadyProducesOwnedDispatch()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Authorized dispatch setup admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "Authorized dispatch must originate from a core EmissionReady"
        );

        const FTSChatDispatchResult Result =
            Coordinator.BeginChatProcessing();
        Require(
            Result.Status == ETSPipelineDispatchStatus::Dispatched,
            "Authorized Chat ready must produce a dispatch"
        );
        Require(Result.Dispatch.has_value(), "Dispatched result must own a dispatch");
        Require(
            Result.Dispatch->Emission.EmissionId
                == Admission.EnqueueResult->AdmittedEmission.EmissionId,
            "Dispatch identity must match the admitted emission"
        );
        Require(
            Result.Dispatch->Emission.Flow == ETSEventFlow::Chat,
            "Chat dispatch must preserve the Chat flow"
        );
        RequireChatInputEqual(
            Result.Dispatch->Payload.Input,
            Input,
            "Owned Chat processing dispatch"
        );

        Require(
            Coordinator.VisitEmissionBinding(
                Result.Dispatch->Emission.EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Dispatched Chat binding must enter Processing"
                    );
                }
            ),
            "Dispatched Chat binding must remain stored"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                Result.Dispatch->Emission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Stored payload after authorized dispatch"
                    );
                }
            ),
            "Dispatched Chat payload must remain stored"
        );
        Require(Coordinator.GetBindingCount() == 1, "Dispatch must retain one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "Dispatch must retain one Chat payload"
        );
    }

    void TestPendingChatCannotReplaceAuthorizedReady()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "Authorized InFlight Chat";
        FTSChatInput SecondInput = MakeCompleteInput();
        SecondInput.Comment = "Pending Chat without ready";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitChat(SecondInput);

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First ready-retention admission must succeed"
        );
        Require(
            Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Second pending admission must succeed"
        );
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "First admission must authorize a ready"
        );
        Require(
            Second.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::NotRequested,
            "Second admission must not replace the pending ready"
        );

        const FTSEmissionId FirstEmissionId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSEmissionId SecondEmissionId =
            Second.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSChatDispatchResult Dispatch =
            Coordinator.BeginChatProcessing();

        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "The retained ready must still dispatch"
        );
        Require(
            Dispatch.Dispatch->Emission.EmissionId == FirstEmissionId,
            "NotRequested must preserve authorization for the first emission"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstEmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Authorized first binding must enter Processing"
                    );
                }
            ),
            "First binding must remain available after dispatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondEmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Pending second binding must remain Bound"
                    );
                }
            ),
            "Second binding must remain available"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                SecondEmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        SecondInput,
                        "Pending second Chat payload"
                    );
                }
            ),
            "Pending second payload must remain intact"
        );
        Require(Coordinator.GetBindingCount() == 2, "Both bindings must remain stored");
        Require(
            Coordinator.GetChatPayloadCount() == 2,
            "Both Chat payloads must remain stored"
        );
    }

    void TestChatDispatchPayloadIsIndependentCopy()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Independent dispatch setup admission must succeed"
        );

        FTSChatDispatchResult Dispatch = Coordinator.BeginChatProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "Independent copy test requires a dispatch"
        );

        Dispatch.Dispatch->Payload.Input.Comment = "Mutated dispatch comment";
        Dispatch.Dispatch->Payload.Input.Emotes.clear();
        Dispatch.Dispatch->Payload.Input.User.Nickname = "Mutated dispatch user";
        Dispatch.Dispatch->Payload.Input.User.UniqueId = "mutated-user-id";

        Require(
            Coordinator.VisitChatPayloadForEmission(
                Admission.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Stored payload after dispatch mutation"
                    );
                }
            ),
            "Stored payload must remain available after dispatch mutation"
        );
    }

    void TestChatReadyDispatchIsConsumedOnce()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Single-consumption setup admission must succeed"
        );

        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSChatDispatchResult FirstDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            FirstDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                FirstDispatch.Dispatch.has_value(),
            "First BeginChatProcessing must dispatch"
        );

        const std::size_t BindingCount = Coordinator.GetBindingCount();
        const std::size_t PayloadCount = Coordinator.GetChatPayloadCount();
        const FTSChatDispatchResult SecondDispatch =
            Coordinator.BeginChatProcessing();

        Require(
            SecondDispatch.Status == ETSPipelineDispatchStatus::NoEmissionReady,
            "Consumed ready must not dispatch twice"
        );
        Require(
            !SecondDispatch.Dispatch.has_value(),
            "Second BeginChatProcessing must not contain a dispatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Consumed ready binding must remain Processing"
                    );
                }
            ),
            "Consumed ready binding must remain stored"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Payload after single ready consumption"
                    );
                }
            ),
            "Consumed ready payload must remain stored"
        );
        Require(
            Coordinator.GetBindingCount() == BindingCount,
            "Second dispatch attempt must not change binding count"
        );
        Require(
            Coordinator.GetChatPayloadCount() == PayloadCount,
            "Second dispatch attempt must not change payload count"
        );
    }

    void TestSuccessfulChatCompletionCleansTerminalState()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Successful completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Successful completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult == ETSProcessingResult::Succeeded,
            "Successful completion metadata mismatch"
        );
        Require(
            Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Succeeded must expose only ConfirmResult"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed,
            "Succeeded must be confirmed by the core"
        );
        Require(
            !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Succeeded lifecycle must start with its Confirmed event"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Successful terminal handling must clean binding and payload"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Successful cleanup must not leave a ready notification"
        );
    }

    void TestSuccessfulChatCompletionCapturesNextReady()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "First successful Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Second successful Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "The first Chat must enter Processing"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == SecondId,
            "Confirm Auto Pump must authorize the second Chat"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1,
            "Only the second Chat must remain stored"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Bound,
                        "Auto Pump ready binding must remain Bound"
                    );
                }
            ),
            "Second Chat binding must remain available"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                SecondId,
                [](const FTSChatPayload&)
                {
                }
            ),
            "Second Chat payload must remain available"
        );
        Require(
            BeginReadyChat(Coordinator) == SecondId,
            "Captured Confirm Auto Pump ready must dispatch once"
        );
    }

    void TestConfirmProcessesTrailingExpirationInCoreOrder()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                true,
                true,
                5s,
                ETSEventExpirePolicy::Discard
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "InFlight before expiration");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Pending expiration");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing before the TTL boundary"
        );

        Clock.Advance(5s);
        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.ConfirmResult.has_value(),
            "Confirm expiration scenario must expose ConfirmResult"
        );
        const FTSConfirmResult& ConfirmResult = *Completion.ConfirmResult;
        Require(
            ConfirmResult.LifecycleEvents.size() == 2,
            "Confirm must report its terminal followed by pending expiration"
        );
        Require(
            ConfirmResult.LifecycleEvents[0].Envelope.EmissionId == FirstId &&
                ConfirmResult.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Confirm lifecycle must begin with the InFlight terminal"
        );
        Require(
            ConfirmResult.LifecycleEvents[1].Envelope.EmissionId == SecondId &&
                ConfirmResult.LifecycleEvents[1].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Confirm lifecycle must preserve the trailing expiration"
        );
        Require(
            ConfirmResult.AutoPumpOutcome.Status ==
                ETSPumpStatus::QueueEmpty,
            "Expired pending Chat must leave Confirm Auto Pump empty"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Confirmed and expired Chats must both be cleaned"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Expiration during Confirm must not leave a ready notification"
        );
    }

    void TestCancelledChatCompletionCleansTerminalState()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Cancelled completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Cancelled completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Cancelled
            );

        Require(
            Completion.ProcessingResult == ETSProcessingResult::Cancelled &&
                !Completion.ConfirmResult.has_value() &&
                Completion.CancelResult.has_value(),
            "Cancelled must expose only CancelResult"
        );
        Require(
            Completion.CancelResult->Status ==
                ETSCancelInFlightStatus::Cancelled &&
                Completion.CancelResult->LifecycleEvents.size() == 1 &&
                Completion.CancelResult->LifecycleEvents[0]
                        .Envelope.EmissionId == EmissionId &&
                Completion.CancelResult->LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Cancelled,
            "Cancelled must report exactly its core terminal"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Cancelled terminal handling must clean binding and payload"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Cancel must not produce an automatic ready"
        );
    }

    void TestFailedChatCompletionUsesCancellationTerminal()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Failed completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Failed completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Failed
            );

        Require(
            Completion.ProcessingResult == ETSProcessingResult::Failed &&
                !Completion.ConfirmResult.has_value() &&
                Completion.CancelResult.has_value(),
            "Failed must preserve Failed and expose only CancelResult"
        );
        Require(
            Completion.CancelResult->Status ==
                ETSCancelInFlightStatus::Cancelled &&
                Completion.CancelResult->LifecycleEvents.size() == 1 &&
                Completion.CancelResult->LifecycleEvents[0]
                        .Envelope.EmissionId == EmissionId &&
                Completion.CancelResult->LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Cancelled,
            "Failed must use the same terminal core cancellation"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Failed terminal handling must clean binding and payload"
        );
    }

    void TestCompletingBoundChatFailsBeforeCoreMutation()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "Processing Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Still Bound Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing"
        );

        bool bThrew = false;
        try
        {
            (void)Coordinator.CompleteChatProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bThrew = true;
        }

        Require(bThrew, "Completing a Bound Chat must throw logic_error");
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 2,
            "Rejected completion must preserve both external authorities"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Rejected completion must preserve Processing"
                    );
                }
            ),
            "Processing binding must remain available"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Rejected completion must preserve Bound"
                    );
                }
            ),
            "Bound binding must remain available"
        );

        const FTSChatProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed,
            "Original InFlight Chat must remain confirmable"
        );
    }

    void TestCancelRequiresExplicitPumpForNextChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "Cancelled first Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Pending second Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Cancelled
            );
        Require(
            Completion.CancelResult.has_value(),
            "Cancelled first Chat must expose CancelResult"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Cancel must not Auto Pump the pending Chat"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == SecondId,
            "Explicit Pump must authorize the pending Chat"
        );
        Require(
            BeginReadyChat(Coordinator) == SecondId,
            "Explicitly pumped Chat must dispatch"
        );
    }

    void TestBusyPumpPreservesPendingReadyNotification()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Ready before busy Pump");

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::Busy &&
                PumpResult.LifecycleEvents.empty(),
            "Pump must report Busy while the ready emission is InFlight"
        );
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Busy Pump must preserve the pending ready notification"
        );
    }

    void TestDiscardExpirationThroughCoordinator()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                false,
                true,
                5s,
                ETSEventExpirePolicy::Discard
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Discard at TTL");

        const FTSNextWakeTimeResult Wake = Coordinator.GetNextWakeTime();
        Require(
            Wake.Status == ETSNextWakeStatus::WakeScheduled &&
                Wake.WakeTime == Clock.Now + 5s,
            "Coordinator must expose the exact Chat wake time"
        );

        Clock.Advance(5s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents[0].Envelope.EmissionId ==
                    EmissionId &&
                Expirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Discard expiration must be forwarded intact"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Discard expiration must clean binding and payload"
        );
        Require(
            Coordinator.GetNextWakeTime().Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Discard cleanup must remove the wake schedule"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Discard expiration must not create a ready notification"
        );
    }

    void TestConsolidateExpirationCleansChatWithoutAccumulation()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                false,
                true,
                5s,
                ETSEventExpirePolicy::Consolidate
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Consolidate at TTL");

        Clock.Advance(5s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents[0].Envelope.EmissionId ==
                    EmissionId &&
                Expirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredConsolidate,
            "Consolidate expiration must be forwarded intact"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Consolidate currently cleans Chat like discard"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Consolidate expiration must not create a ready notification"
        );

        const FTSProcessDueExpirationsResult Repeated =
            Coordinator.ProcessDueExpirations();
        Require(
            Repeated.LifecycleEvents.empty() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Consolidate must not accumulate or repeat terminal state"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterChatPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Chat candidate and admission defaults", &TestChatCandidateAndAdmissionDefaults});
        Tests.push_back({"Chat payload snapshot and input preservation", &TestChatPayloadSnapshotAndInputPreservation});
    }

    void RegisterChatPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Chat coordinator first admission Auto Pumps", &TestChatCoordinatorFirstAdmissionAutoPumps});
        Tests.push_back({"Chat coordinator second admission while busy", &TestChatCoordinatorSecondAdmissionWhileBusy});
        Tests.push_back({"Chat coordinator rejects disabled flow", &TestChatCoordinatorRejectsDisabledFlow});
        Tests.push_back({"Chat coordinator capacity rejection preserves prior admission", &TestChatCoordinatorCapacityRejectionPreservesPriorAdmission});
        Tests.push_back({"Chat coordinator unknown emission inspection", &TestChatCoordinatorUnknownEmissionInspection});
        Tests.push_back({"Chat dispatch without pending ready", &TestChatDispatchWithoutPendingReady});
        Tests.push_back({"Authorized Chat ready produces owned dispatch", &TestAuthorizedChatReadyProducesOwnedDispatch});
        Tests.push_back({"Pending Chat cannot replace authorized ready", &TestPendingChatCannotReplaceAuthorizedReady});
        Tests.push_back({"Chat dispatch payload is an independent copy", &TestChatDispatchPayloadIsIndependentCopy});
        Tests.push_back({"Chat ready dispatch is consumed once", &TestChatReadyDispatchIsConsumedOnce});
        Tests.push_back({"Successful Chat completion cleans terminal state", &TestSuccessfulChatCompletionCleansTerminalState});
        Tests.push_back({"Successful Chat completion captures next ready", &TestSuccessfulChatCompletionCapturesNextReady});
        Tests.push_back({"Confirm processes trailing expiration in core order", &TestConfirmProcessesTrailingExpirationInCoreOrder});
        Tests.push_back({"Cancelled Chat completion cleans terminal state", &TestCancelledChatCompletionCleansTerminalState});
        Tests.push_back({"Failed Chat completion uses cancellation terminal", &TestFailedChatCompletionUsesCancellationTerminal});
        Tests.push_back({"Completing Bound Chat fails before core mutation", &TestCompletingBoundChatFailsBeforeCoreMutation});
        Tests.push_back({"Cancel requires explicit Pump for next Chat", &TestCancelRequiresExplicitPumpForNextChat});
        Tests.push_back({"Busy Pump preserves pending ready notification", &TestBusyPumpPreservesPendingReadyNotification});
        Tests.push_back({"Discard expiration through coordinator", &TestDiscardExpirationThroughCoordinator});
        Tests.push_back({"Consolidate expiration cleans without accumulation", &TestConsolidateExpirationCleansChatWithoutAccumulation});
    }
}
