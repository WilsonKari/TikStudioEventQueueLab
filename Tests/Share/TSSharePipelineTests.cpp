#include "EventPipeline/Families/TSShareFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    void TestShareCandidateAndAdmissionDefaults()
    {
        FTSShareInput Input;
        Input.User.UniqueId = "share-user";

        const TTSFamilyDecision<FTSSharePayload> Decision =
            FTSShareFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Share must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSSharePayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Share,
            "Share candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Share,
            "Share candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::ShareMilestone,
            "Share candidate must not infer a milestone"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Share candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Share candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride.count() == 0,
            "Share candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Share candidate must not request special eviction protection"
        );
    }

    void TestSharePayloadSnapshotAndInputPreservation()
    {
        FTSShareInput Input = MakeCompleteShareInput();
        const FTSShareInput Original = Input;

        const TTSFamilyDecision<FTSSharePayload> Decision =
            FTSShareFamily::Decide(Input);

        Require(Decision.has_value(), "Share snapshot must produce a candidate");
        RequireShareInputEqual(
            Input,
            Original,
            "Share input after by-copy decision"
        );

        Input.User.UniqueId = "mutated-share-user";
        Input.User.Nickname = "Mutated Share User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireShareInputEqual(
            Decision->Payload.Input,
            Original,
            "Share payload snapshot"
        );
    }

    void TestShareFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSShareInput ExpectedInput = MakeCompleteShareInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShare(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First Share admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow == ETSEventFlow::Share,
            "First Share admission envelope mismatch"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status ==
                ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First Share admission must Auto Pump"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 1,
            "First Share admission authority counts mismatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Share &&
                            Binding.ExpectedFlow == ETSEventFlow::Share &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "First Share binding mismatch"
                    );
                }
            ),
            "First Share binding must exist"
        );
        Require(
            Coordinator.VisitSharePayloadForEmission(
                EmissionId,
                [&](const FTSSharePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First Share payload"
                    );
                }
            ),
            "First Share payload must exist"
        );

        Require(
            BeginReadyShare(Coordinator) == EmissionId,
            "First Share must enter Processing"
        );
        const FTSShareProcessingCompletionResult Completion =
            Coordinator.CompleteShareProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed,
            "First Share completion must succeed"
        );
    }

    void TestShareSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitShare(MakeCompleteShareInput());
        FTSShareInput SecondInput = MakeCompleteShareInput();
        SecondInput.User.UniqueId = "second-share-user";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitShare(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both Share admissions must be accepted"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSEmissionId SecondId =
            Second.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status ==
                ETSPumpStatus::EmissionReady &&
                Second.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested,
            "Busy Share admission must not replace the first ready"
        );
        Require(
            FirstId != SecondId &&
                Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetSharePayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share,
            "Busy Share admissions must preserve distinct authorities"
        );

        Require(
            BeginReadyShare(Coordinator) == FirstId,
            "Busy Share admission must preserve the first ready"
        );
        const FTSShareProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteShareProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == SecondId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share,
            "First Share confirmation must expose the second ready"
        );

        Require(
            BeginReadyShare(Coordinator) == SecondId,
            "Second Share must become dispatchable"
        );
        const FTSShareProcessingCompletionResult SecondCompletion =
            Coordinator.CompleteShareProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        Require(
            SecondCompletion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Both Share admissions must finish without retained authorities"
        );
    }

    void TestDisabledShareFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(MakeShareSettings(false, 10));
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShare(MakeCompleteShareInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled Share flow must be rejected by the core"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled Share rejection must remove its provisional payload"
        );
    }

    void TestShareCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings = MakeShareSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSShareInput FirstInput = MakeCompleteShareInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitShare(FirstInput);
        FTSShareInput RejectedInput = MakeCompleteShareInput();
        RejectedInput.User.UniqueId = "rejected-share-user";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitShare(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity Share admission must succeed"
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second Share admission must reject at capacity"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 1,
            "Capacity rejection must preserve only the prior Share"
        );
        Require(
            Coordinator.VisitSharePayloadForEmission(
                FirstId,
                [&](const FTSSharePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity Share"
                    );
                }
            ),
            "Prior Share payload must remain available"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == FirstId,
            "Explicit Pump must select the preserved Share"
        );
        Require(
            BeginReadyShare(Coordinator) == FirstId,
            "Preserved Share must enter Processing"
        );
        (void)Coordinator.CompleteShareProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0,
            "Capacity scenario must finish without retained authorities"
        );
    }

    void TestShareDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSShareInput Input = MakeCompleteShareInput();
        const FTSShareInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShare(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned Share dispatch admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;

        Input.User.UniqueId = "mutated-caller-share-user";
        Input.User.Nickname = "Mutated caller Share";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        FTSShareDispatchResult DispatchResult =
            Coordinator.BeginShareProcessing();
        Require(
            DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched &&
                DispatchResult.Dispatch.has_value(),
            "Authorized Share ready must dispatch"
        );
        Require(
            DispatchResult.Dispatch->Emission.EmissionId == EmissionId &&
                DispatchResult.Dispatch->Emission.Flow == ETSEventFlow::Share,
            "Share dispatch envelope mismatch"
        );
        RequireShareInputEqual(
            DispatchResult.Dispatch->Payload.Input,
            ExpectedInput,
            "Share dispatch payload"
        );

        DispatchResult.Dispatch->Payload.Input.User.UniqueId =
            "mutated-dispatch-share-user";
        DispatchResult.Dispatch->Payload.Input.User.Nickname =
            "Mutated Share dispatch copy";
        DispatchResult.Dispatch->Payload.Input.User.ProfilePictureUrl.clear();
        DispatchResult.Dispatch->Payload.Input.User.FollowRole = 0;
        DispatchResult.Dispatch->Payload.Input.User.bIsModerator = false;
        DispatchResult.Dispatch->Payload.Input.User.bIsSubscriber = true;
        DispatchResult.Dispatch->Payload.Input.User.bIsNewGifter = false;
        DispatchResult.Dispatch->Payload.Input.User.TopGifterRank = 0;
        DispatchResult.Dispatch->Payload.Input.User.GifterLevel = 0;
        DispatchResult.Dispatch->Payload.Input.User.TeamMemberLevel = 0;

        Require(
            Coordinator.VisitSharePayloadForEmission(
                EmissionId,
                [&](const FTSSharePayload& StoredPayload)
                {
                    RequireShareInputEqual(
                        StoredPayload.Input,
                        ExpectedInput,
                        "Stored Share after dispatch mutation"
                    );
                }
            ),
            "Stored Share must remain after dispatch mutation"
        );

        const FTSShareDispatchResult SecondDispatch =
            Coordinator.BeginShareProcessing();
        Require(
            SecondDispatch.Status ==
                ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value() &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Share dispatch must consume its ready exactly once"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Share dispatch must authorize Processing"
                    );
                }
            ) && Coordinator.GetSharePayloadCount() == 1,
            "Dispatched Share authorities must remain available"
        );

        (void)Coordinator.CompleteShareProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongFamilyBeginPreservesReadyShare()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "wrong-begin-share-user");

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::NoEmissionReady &&
                !ChatDispatch.Dispatch.has_value(),
            "Chat Begin must not dispatch a Share ready"
        );
        const FTSFollowDispatchResult FollowDispatch =
            Coordinator.BeginFollowProcessing();
        Require(
            FollowDispatch.Status ==
                ETSPipelineDispatchStatus::NoEmissionReady &&
                !FollowDispatch.Dispatch.has_value(),
            "Follow Begin must not dispatch a Share ready"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                ETSEventFamilyKind::Share &&
                Coordinator.GetSharePayloadCount() == 1,
            "Wrong-family Begin must preserve the Share ready and payload"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ShareId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Wrong-family Begin must preserve Share Bound"
                    );
                }
            ),
            "Wrong-family Begin must preserve the Share binding"
        );

        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Share Begin must dispatch the preserved ready"
        );
        (void)Coordinator.CompleteShareProcessing(
            ShareId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestShareSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedShare(Coordinator, "successful-share-user");
        Require(
            BeginReadyShare(Coordinator) == EmissionId,
            "Successful Share must enter Processing"
        );

        const FTSShareProcessingCompletionResult Completion =
            Coordinator.CompleteShareProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult == ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Successful Share completion result mismatch"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful Share completion terminal mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitSharePayloadForEmission(
                    EmissionId,
                    [](const FTSSharePayload&)
                    {
                    }
                ) &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Successful Share completion must clean terminal authorities"
        );
    }

    void TestShareCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminalScenario = [](ETSProcessingResult ProcessingResult)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId EmissionId = SubmitAcceptedShare(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-share-user"
                    : "failed-share-user"
            );
            Require(
                BeginReadyShare(Coordinator) == EmissionId,
                "Terminal Share must enter Processing"
            );

            const FTSShareProcessingCompletionResult Completion =
                Coordinator.CompleteShareProcessing(
                    EmissionId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == EmissionId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed Share must expose only CancelResult"
            );
            Require(
                Completion.CancelResult->Status ==
                    ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == EmissionId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed Share must use cancellation terminal"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetSharePayloadCount() == 0 &&
                    !Coordinator.PeekPendingReadyFamilyKind().has_value(),
                "Cancellation terminal must clean Share authorities"
            );
        };

        RunTerminalScenario(ETSProcessingResult::Cancelled);
        RunTerminalScenario(ETSProcessingResult::Failed);
    }

    void TestWrongFamilyCompletionPreservesShareInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "wrong-completion-share-user");
        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Wrong-family completion Share must enter Processing"
        );

        bool bChatThrew = false;
        try
        {
            (void)Coordinator.CompleteChatProcessing(
                ShareId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bChatThrew = true;
        }
        Require(bChatThrew, "Chat completion must reject a Share binding");

        bool bFollowThrew = false;
        try
        {
            (void)Coordinator.CompleteFollowProcessing(
                ShareId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bFollowThrew = true;
        }
        Require(bFollowThrew, "Follow completion must reject a Share binding");

        Require(
            Coordinator.VisitEmissionBinding(
                ShareId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Wrong completion must preserve Share Processing"
                    );
                }
            ) &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 1,
            "Wrong completion must preserve Share authorities"
        );
        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with the Share InFlight"
        );

        const FTSShareProcessingCompletionResult Completion =
            Coordinator.CompleteShareProcessing(
                ShareId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0,
            "Correct Share completion must clean preserved authorities"
        );
    }

    void TestShareCompletionCapturesReadyChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "share-before-chat-user");
        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Share must enter Processing before Chat admission"
        );

        FTSChatInput ExpectedChat = MakeCompleteInput();
        ExpectedChat.Comment = "Chat after Share";
        const FTSPipelineAdmissionResult ChatAdmission =
            Coordinator.SubmitChat(ExpectedChat);
        Require(
            ChatAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ChatAdmission.EnqueueResult.has_value(),
            "Chat after Share must be accepted"
        );
        const FTSEmissionId ChatId =
            ChatAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSShareProcessingCompletionResult Completion =
            Coordinator.CompleteShareProcessing(
                ShareId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == ChatId,
            "Share confirmation must expose the next Chat ready"
        );
        Require(
            Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat,
            "Share completion must retain only ready Chat authorities"
        );

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ChatDispatch.Dispatch.has_value() &&
                ChatDispatch.Dispatch->Emission.EmissionId == ChatId,
            "Chat selected after Share must dispatch"
        );
        RequireChatPayloadMatchesInput(
            ChatDispatch.Dispatch->Payload,
            ExpectedChat,
            "Chat after Share payload"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestFollowCompletionCapturesReadyShare()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FollowId =
            SubmitAcceptedFollow(Coordinator, "follow-before-share-user");
        Require(
            BeginReadyFollow(Coordinator) == FollowId,
            "Follow must enter Processing before Share admission"
        );

        FTSShareInput ExpectedShare = MakeCompleteShareInput();
        ExpectedShare.User.UniqueId = "share-after-follow-user";
        const FTSPipelineAdmissionResult ShareAdmission =
            Coordinator.SubmitShare(ExpectedShare);
        Require(
            ShareAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ShareAdmission.EnqueueResult.has_value(),
            "Share after Follow must be accepted"
        );
        const FTSEmissionId ShareId =
            ShareAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSFollowProcessingCompletionResult Completion =
            Coordinator.CompleteFollowProcessing(
                FollowId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == ShareId &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share,
            "Follow confirmation must expose the pending Share"
        );

        const FTSShareDispatchResult ShareDispatch =
            Coordinator.BeginShareProcessing();
        Require(
            ShareDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ShareDispatch.Dispatch.has_value() &&
                ShareDispatch.Dispatch->Emission.EmissionId == ShareId,
            "Share selected after Follow must dispatch"
        );
        RequireShareInputEqual(
            ShareDispatch.Dispatch->Payload.Input,
            ExpectedShare,
            "Share after Follow payload"
        );
        (void)Coordinator.CompleteShareProcessing(
            ShareId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestPendingShareExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalShareSettings(
            true,
            true,
            5s,
            ETSEventExpirePolicy::Discard
        );
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Expiration Chat settings must exist");
        ChatSettings->bEnabled = true;
        ChatSettings->MaxSlots = 10;
        ChatSettings->TTL = 30s;

        FTSEventPipelineCoordinator Coordinator(
            std::move(Settings),
            Clock.MakeProvider()
        );
        const FTSEmissionId ChatId =
            SubmitAcceptedChat(Coordinator, "Processing while Share expires");
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Share admission"
        );
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "expiring-share-user");

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    ShareId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::Share &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Share expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    ShareId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Expired Share must leave only the Processing Chat authorities"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Share expiration must preserve Chat Processing"
                    );
                }
            ),
            "Processing Chat binding must remain available"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Chat after Share expiration"
        );

        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterSharePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Share candidate and admission defaults", &TestShareCandidateAndAdmissionDefaults});
        Tests.push_back({"Share payload snapshot and input preservation", &TestSharePayloadSnapshotAndInputPreservation});
    }

    void RegisterSharePipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Share first admission Auto Pumps", &TestShareFirstAdmissionAutoPumps});
        Tests.push_back({"Share second admission while busy", &TestShareSecondAdmissionWhileBusy});
        Tests.push_back({"Disabled Share flow rejects without leaks", &TestDisabledShareFlowRejectsWithoutLeaks});
        Tests.push_back({"Share capacity rejection removes provisional payload", &TestShareCapacityRejectionRemovesProvisionalPayload});
        Tests.push_back({"Share dispatch owns snapshot and is one shot", &TestShareDispatchOwnsSnapshotAndIsOneShot});
        Tests.push_back({"Wrong-family Begin preserves ready Share", &TestWrongFamilyBeginPreservesReadyShare});
        Tests.push_back({"Share success completion confirms and cleans", &TestShareSuccessCompletionConfirmsAndCleans});
        Tests.push_back({"Share cancel and failure clean terminal state", &TestShareCancelAndFailureCleanTerminalState});
        Tests.push_back({"Wrong-family completion preserves Share InFlight", &TestWrongFamilyCompletionPreservesShareInFlight});
        Tests.push_back({"Share completion captures ready Chat", &TestShareCompletionCapturesReadyChat});
        Tests.push_back({"Follow completion captures ready Share", &TestFollowCompletionCapturesReadyShare});
        Tests.push_back({"Pending Share expires while Chat is Processing", &TestPendingShareExpiresWhileChatIsProcessing});
    }
}
