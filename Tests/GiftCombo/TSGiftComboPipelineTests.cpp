#include "EventPipeline/Families/TSGiftComboFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void TestGiftComboProducesDirectGiftComboCandidate()
    {
        FTSGiftInput Input = MakeCompleteGiftInput();
        Input.DiamondCount = 777;
        Input.RepeatCount = 12;
        Input.GiftType = 7;
        Input.bRepeatEnd = true;
        Input.GroupId = "gift-combo-direct-group";

        const TTSFamilyDecision<FTSGiftComboPayload> Decision =
            FTSGiftComboFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "GiftCombo must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSGiftComboPayload>& Candidate =
            *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Gift,
            "GiftCombo must remain in the Gift domain"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::GiftCombo &&
                Candidate.EnqueueRequest.Flow != ETSEventFlow::Gift,
            "GiftCombo candidate Flow mismatch"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Gift,
                ETSEventFlow::Gift
            ),
            "Gift/Gift must remain authorized"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Gift,
                ETSEventFlow::GiftCombo
            ),
            "Gift/GiftCombo must be authorized in phase B"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "GiftCombo candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL &&
                Candidate.EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0},
            "GiftCombo candidate TTL defaults mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "GiftCombo candidate must not request eviction protection"
        );
        RequireGiftInputEqual(
            Candidate.Payload.Input,
            Input,
            "GiftCombo direct payload"
        );
    }

    void TestGiftComboPayloadSnapshotAndMetadataIndependence()
    {
        FTSGiftInput FirstInput = MakeCompleteGiftInput();
        FirstInput.DiamondCount = 345;
        FirstInput.RepeatCount = 9;
        FirstInput.GiftType = 4;
        FirstInput.bRepeatEnd = true;
        FirstInput.GroupId = "gift-combo-first-group";
        const FTSGiftInput FirstExpected = FirstInput;

        const TTSFamilyDecision<FTSGiftComboPayload> FirstDecision =
            FTSGiftComboFamily::Decide(FirstInput);
        Require(
            FirstDecision.has_value(),
            "First GiftCombo decision must produce a candidate"
        );
        RequireGiftInputEqual(
            FirstInput,
            FirstExpected,
            "First GiftCombo caller input"
        );

        FirstInput.GiftId = 0;
        FirstInput.GiftName = "mutated-first-gift";
        FirstInput.GiftPictureUrl = "mutated-first-picture";
        FirstInput.DiamondCount = 0;
        FirstInput.RepeatCount = 0;
        FirstInput.GiftType = 0;
        FirstInput.Describe = "mutated-first-description";
        FirstInput.bRepeatEnd = false;
        FirstInput.GroupId = "mutated-first-group";
        FirstInput.User.UniqueId = "mutated-first-user";
        FirstInput.User.Nickname = "Mutated First User";
        FirstInput.User.ProfilePictureUrl = "mutated-first-user-picture";
        FirstInput.User.FollowRole = 0;
        FirstInput.User.bIsModerator = false;
        FirstInput.User.bIsSubscriber = true;
        FirstInput.User.bIsNewGifter = false;
        FirstInput.User.TopGifterRank = 0;
        FirstInput.User.GifterLevel = 0;
        FirstInput.User.TeamMemberLevel = 0;

        RequireGiftInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First GiftCombo payload snapshot"
        );

        FTSGiftInput SecondInput = MakeCompleteGiftInput();
        SecondInput.DiamondCount = 901;
        SecondInput.RepeatCount = 31;
        SecondInput.GiftType = 8;
        SecondInput.bRepeatEnd = false;
        SecondInput.GroupId = "gift-combo-second-group";
        SecondInput.User.UniqueId = "gift-combo-second-user";
        SecondInput.User.Nickname = "Second Combo User";
        SecondInput.User.ProfilePictureUrl =
            "https://example.test/second-combo-user.png";
        SecondInput.User.FollowRole = 5;
        SecondInput.User.bIsModerator = false;
        SecondInput.User.bIsSubscriber = true;
        SecondInput.User.bIsNewGifter = false;
        SecondInput.User.TopGifterRank = 21;
        SecondInput.User.GifterLevel = 22;
        SecondInput.User.TeamMemberLevel = 23;
        const FTSGiftInput SecondExpected = SecondInput;

        const TTSFamilyDecision<FTSGiftComboPayload> SecondDecision =
            FTSGiftComboFamily::Decide(SecondInput);
        Require(
            SecondDecision.has_value(),
            "Second GiftCombo decision must produce a candidate"
        );
        Require(
            FirstDecision->FamilyKind == ETSEventFamilyKind::Gift &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Gift &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::GiftCombo &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::GiftCombo &&
                FirstDecision->EnqueueRequest.Flow != ETSEventFlow::Gift &&
                SecondDecision->EnqueueRequest.Flow != ETSEventFlow::Gift,
            "GiftCombo metadata must be stable across independent decisions"
        );

        SecondInput.GiftId = -1;
        SecondInput.GiftName = "mutated-second-gift";
        SecondInput.GiftPictureUrl = "mutated-second-picture";
        SecondInput.DiamondCount = -1;
        SecondInput.RepeatCount = -1;
        SecondInput.GiftType = -1;
        SecondInput.Describe = "mutated-second-description";
        SecondInput.bRepeatEnd = true;
        SecondInput.GroupId = "mutated-second-group";
        SecondInput.User.UniqueId = "mutated-second-user";
        SecondInput.User.Nickname = "Mutated Second User";
        SecondInput.User.ProfilePictureUrl = "mutated-second-user-picture";
        SecondInput.User.FollowRole = -1;
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        SecondInput.User.TopGifterRank = -1;
        SecondInput.User.GifterLevel = -1;
        SecondInput.User.TeamMemberLevel = -1;

        RequireGiftInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second GiftCombo payload snapshot"
        );
        RequireUserEqual(
            SecondDecision->Payload.Input.User,
            SecondExpected.User,
            "Second GiftCombo user snapshot"
        );
        RequireGiftInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "Independent first GiftCombo payload"
        );
    }

    void RequireGiftComboBinding(
        const FTSEventPipelineCoordinator& Coordinator,
        FTSEmissionId EmissionId,
        ETSExternalEmissionState ExpectedState
    )
    {
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [&](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.EmissionId == EmissionId &&
                            Binding.FamilyKind == ETSEventFamilyKind::Gift &&
                            Binding.ExpectedFlow ==
                                ETSEventFlow::GiftCombo &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState == ExpectedState,
                        "GiftCombo binding mismatch"
                    );
                }
            ),
            "GiftCombo binding must exist"
        );
    }

    void RequireNoGiftAuthorities(
        const FTSEventPipelineCoordinator& Coordinator,
        const std::string& Context
    )
    {
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value() &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            Context
        );
    }

    void TestGiftComboFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSGiftInput ExpectedInput = MakeCompleteGiftInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGiftCombo(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First GiftCombo admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow ==
                    ETSEventFlow::GiftCombo &&
                CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First GiftCombo admission envelope mismatch"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::GiftCombo &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 1,
            "First GiftCombo ready metadata mismatch"
        );
        RequireGiftComboBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Bound
        );
        Require(
            Coordinator.VisitGiftComboPayloadForEmission(
                EmissionId,
                [&](const FTSGiftComboPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First GiftCombo payload"
                    );
                }
            ),
            "First GiftCombo payload must exist"
        );
        Coordinator.ValidateInternalConsistency();

        Require(
            BeginReadyGiftCombo(Coordinator) == EmissionId,
            "First GiftCombo must enter Processing"
        );
        const FTSGiftComboProcessingCompletionResult Completion =
            Coordinator.CompleteGiftComboProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed,
            "First GiftCombo must confirm"
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "First GiftCombo completion must clean authorities"
        );
    }

    void TestGiftComboSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeGiftComboSettings(true, 10)
        );
        FTSGiftInput FirstInput = MakeCompleteGiftInput();
        FirstInput.User.UniqueId = "first-combo-user";
        FirstInput.GroupId = "first-combo-group";
        FirstInput.RepeatCount = 3;
        FirstInput.DiamondCount = 30;
        const FTSGiftInput ExpectedFirst = FirstInput;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitGiftCombo(std::move(FirstInput));

        FTSGiftInput SecondInput = MakeCompleteGiftInput();
        SecondInput.User.UniqueId = "second-combo-user";
        SecondInput.GroupId = "second-combo-group";
        SecondInput.RepeatCount = 11;
        SecondInput.DiamondCount = 110;
        const FTSGiftInput ExpectedSecond = SecondInput;
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitGiftCombo(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both GiftCombo admissions must be accepted"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSEmissionId SecondId =
            Second.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            FirstId != SecondId &&
                First.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Second.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::GiftCombo,
            "Busy GiftCombo admissions must preserve FIFO authorities"
        );
        Require(
            Coordinator.VisitGiftComboPayloadForEmission(
                FirstId,
                [&](const FTSGiftComboPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        ExpectedFirst,
                        "First busy GiftCombo"
                    );
                }
            ) &&
                Coordinator.VisitGiftComboPayloadForEmission(
                    SecondId,
                    [&](const FTSGiftComboPayload& Payload)
                    {
                        RequireGiftInputEqual(
                            Payload.Input,
                            ExpectedSecond,
                            "Second busy GiftCombo"
                        );
                    }
                ),
            "Busy GiftCombo snapshots must remain distinct"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == FirstId,
            "First GiftCombo must preserve FIFO order"
        );
        const FTSGiftComboProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteGiftComboProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::GiftCombo &&
                Coordinator.GetGiftComboPayloadCount() == 1,
            "First completion must expose the second GiftCombo"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == SecondId,
            "Second GiftCombo must become ready"
        );
        (void)Coordinator.CompleteGiftComboProcessing(
            SecondId,
            ETSProcessingResult::Succeeded
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "Busy GiftCombo scenario must clean authorities"
        );
    }

    void TestDisabledGiftComboFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeGiftComboSettings(false, 1)
        );
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGiftCombo(MakeCompleteGiftInput());
        Require(
            Admission.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled GiftCombo must be rejected"
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "Disabled GiftCombo must not leak authorities"
        );
    }

    void TestGiftComboCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings =
            MakeGiftComboSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSGiftInput FirstInput = MakeCompleteGiftInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitGiftCombo(FirstInput);
        FTSGiftInput RejectedInput = MakeCompleteGiftInput();
        RejectedInput.User.UniqueId = "rejected-combo-user";
        RejectedInput.GroupId = "rejected-combo-group";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitGiftCombo(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Rejected.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 1,
            "GiftCombo capacity rejection must remove provisional payload"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.VisitGiftComboPayloadForEmission(
                FirstId,
                [&](const FTSGiftComboPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity GiftCombo"
                    );
                }
            ),
            "First capacity GiftCombo must remain"
        );
        Require(
            Coordinator.Pump().Outcome.Status ==
                ETSPumpStatus::EmissionReady,
            "Preserved GiftCombo must become ready"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == FirstId,
            "Preserved GiftCombo must dispatch"
        );
        (void)Coordinator.CompleteGiftComboProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "GiftCombo capacity scenario must clean authorities"
        );
    }

    void TestGiftComboDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSGiftInput Input = MakeCompleteGiftInput();
        const FTSGiftInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGiftCombo(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned GiftCombo admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        Input.GiftName = "mutated caller";
        Input.GroupId = "mutated caller group";
        Input.User.UniqueId = "mutated-caller-user";

        FTSGiftComboDispatchResult Dispatch =
            Coordinator.BeginGiftComboProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value() &&
                Dispatch.Dispatch->Emission.EmissionId == EmissionId &&
                Dispatch.Dispatch->Emission.Flow ==
                    ETSEventFlow::GiftCombo,
            "GiftCombo ready must dispatch once"
        );
        RequireGiftInputEqual(
            Dispatch.Dispatch->Payload.Input,
            ExpectedInput,
            "Owned GiftCombo dispatch"
        );
        Dispatch.Dispatch->Payload.Input.GiftName = "mutated dispatch";
        Dispatch.Dispatch->Payload.Input.GroupId = "mutated dispatch group";
        Dispatch.Dispatch->Payload.Input.User.UniqueId =
            "mutated-dispatch-user";
        Require(
            Coordinator.VisitGiftComboPayloadForEmission(
                EmissionId,
                [&](const FTSGiftComboPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "Stored GiftCombo after dispatch mutation"
                    );
                }
            ),
            "Stored GiftCombo must remain during Processing"
        );
        const FTSGiftComboDispatchResult Second =
            Coordinator.BeginGiftComboProcessing();
        Require(
            Second.Status == ETSPipelineDispatchStatus::NoEmissionReady &&
                !Second.Dispatch.has_value() &&
                Coordinator.GetGiftComboPayloadCount() == 1,
            "GiftCombo dispatch must consume ready exactly once"
        );
        RequireGiftComboBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Processing
        );
        (void)Coordinator.CompleteGiftComboProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongRouteBeginPreservesReadyGiftCombo()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "wrong-begin-combo-user"
        );
        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                        ETSEventFamilyKind::Gift &&
                    Coordinator.PeekPendingReadyFlow() ==
                        ETSEventFlow::GiftCombo &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetGiftPayloadCount() == 0 &&
                    Coordinator.GetGiftComboPayloadCount() == 1,
                "Wrong-route Begin must preserve GiftCombo ready"
            );
            RequireGiftComboBinding(
                Coordinator,
                GiftComboId,
                ETSExternalEmissionState::Bound
            );
        };
        const auto RequireNoDispatch = [&](const auto& Result)
        {
            Require(
                Result.Status ==
                        ETSPipelineDispatchStatus::NoEmissionReady &&
                    !Result.Dispatch.has_value(),
                "Wrong-route Begin must not dispatch GiftCombo"
            );
            RequirePreserved();
        };

        RequireNoDispatch(Coordinator.BeginChatProcessing());
        RequireNoDispatch(Coordinator.BeginFollowProcessing());
        RequireNoDispatch(Coordinator.BeginShareProcessing());
        RequireNoDispatch(Coordinator.BeginLikeProcessing());
        RequireNoDispatch(Coordinator.BeginRoomUserProcessing());
        RequireNoDispatch(Coordinator.BeginGiftProcessing());
        RequireNoDispatch(Coordinator.BeginMemberProcessing());
        Require(
            BeginReadyGiftCombo(Coordinator) == GiftComboId,
            "Correct GiftCombo Begin must consume preserved ready"
        );
        (void)Coordinator.CompleteGiftComboProcessing(
            GiftComboId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestGiftComboSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "successful-combo-user"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == GiftComboId,
            "Successful GiftCombo must enter Processing"
        );
        const FTSGiftComboProcessingCompletionResult Completion =
            Coordinator.CompleteGiftComboProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == GiftComboId &&
                Completion.ProcessingResult ==
                    ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == GiftComboId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful GiftCombo lifecycle mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                GiftComboId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitGiftComboPayloadForEmission(
                    GiftComboId,
                    [](const FTSGiftComboPayload&)
                    {
                    }
                ),
            "Successful GiftCombo must remove binding and payload"
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "Successful GiftCombo completion must clean authorities"
        );
    }

    void TestGiftComboCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminal = [](ETSProcessingResult ProcessingResult)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-combo-user"
                    : "failed-combo-user"
            );
            Require(
                BeginReadyGiftCombo(Coordinator) == GiftComboId,
                "Terminal GiftCombo must enter Processing"
            );
            const FTSGiftComboProcessingCompletionResult Completion =
                Coordinator.CompleteGiftComboProcessing(
                    GiftComboId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == GiftComboId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value() &&
                    Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == GiftComboId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed GiftCombo terminal mismatch"
            );
            RequireNoGiftAuthorities(
                Coordinator,
                "Terminal GiftCombo must clean without retry"
            );
        };

        RunTerminal(ETSProcessingResult::Cancelled);
        RunTerminal(ETSProcessingResult::Failed);
    }

    void TestWrongRouteCompletionPreservesGiftComboInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "wrong-completion-combo-user"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == GiftComboId,
            "Wrong-route GiftCombo must enter Processing"
        );
        const auto RequirePreserved = [&]()
        {
            RequireGiftComboBinding(
                Coordinator,
                GiftComboId,
                ETSExternalEmissionState::Processing
            );
            Require(
                Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetGiftPayloadCount() == 0 &&
                    Coordinator.GetGiftComboPayloadCount() == 1 &&
                    Coordinator.Pump().Outcome.Status ==
                        ETSPumpStatus::Busy,
                "Wrong completion must preserve GiftCombo InFlight"
            );
        };
        const auto RequireRejected = [&](auto&& Callback)
        {
            bool bRejected = false;
            try
            {
                Callback();
            }
            catch (const FTSRejectedProcessingCompletionError&)
            {
                bRejected = true;
            }
            Require(
                bRejected,
                "Wrong-route completion must be definitively rejected"
            );
            RequirePreserved();
        };

        RequireRejected([&]()
        {
            (void)Coordinator.CompleteChatProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteFollowProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteShareProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteLikeProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteRoomUserProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteGiftProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteMemberProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        });

        const FTSGiftComboProcessingCompletionResult Completion =
            Coordinator.CompleteGiftComboProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value(),
            "Correct GiftCombo completion must succeed"
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "Correct GiftCombo completion must clean authorities"
        );
    }

    void TestGiftComboCompletionCapturesReadyGift()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "combo-before-gift-user"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == GiftComboId,
            "GiftCombo must process before Gift"
        );
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "gift-after-combo-user");
        const FTSGiftComboProcessingCompletionResult Completion =
            Coordinator.CompleteGiftComboProcessing(
                GiftComboId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == GiftId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::Gift &&
                Coordinator.GetGiftComboPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 1,
            "GiftCombo completion must expose direct Gift"
        );
        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Direct Gift must dispatch after GiftCombo"
        );
        (void)Coordinator.CompleteGiftProcessing(
            GiftId,
            ETSProcessingResult::Succeeded
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "GiftCombo then Gift must clean authorities"
        );
    }

    void TestGiftCompletionCapturesReadyGiftCombo()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "gift-before-combo-user");
        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Gift must process before GiftCombo"
        );
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "combo-after-gift-user"
        );
        const FTSGiftProcessingCompletionResult Completion =
            Coordinator.CompleteGiftProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == GiftComboId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::GiftCombo &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 1,
            "Gift completion must expose GiftCombo"
        );
        Require(
            BeginReadyGiftCombo(Coordinator) == GiftComboId,
            "GiftCombo must dispatch after direct Gift"
        );
        (void)Coordinator.CompleteGiftComboProcessing(
            GiftComboId,
            ETSProcessingResult::Succeeded
        );
        RequireNoGiftAuthorities(
            Coordinator,
            "Gift then GiftCombo must clean authorities"
        );
    }

    void TestPendingGiftComboExpiresWhileChatIsProcessing()
    {
        using namespace std::chrono_literals;

        FControlledClock Clock;
        FTSEventQueueSettings Settings =
            MakeOperationalGiftComboSettings(
                true,
                true,
                5s,
                ETSEventExpirePolicy::Discard
            );
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(
            ChatSettings != nullptr,
            "Expiration Chat settings must exist"
        );
        ChatSettings->bEnabled = true;
        ChatSettings->MaxSlots = 10;
        ChatSettings->TTL = 30s;
        ChatSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;

        FTSEventPipelineCoordinator Coordinator(
            std::move(Settings),
            Clock.MakeProvider()
        );
        const FTSEmissionId ChatId = SubmitAcceptedChat(
            Coordinator,
            "Processing while GiftCombo expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must process before GiftCombo"
        );
        const FTSEmissionId GiftComboId = SubmitAcceptedGiftCombo(
            Coordinator,
            "expiring-combo-user"
        );
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            "GiftCombo must remain Pending while Chat processes"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    GiftComboId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::GiftCombo &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending GiftCombo expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    GiftComboId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.VisitGiftComboPayloadForEmission(
                    GiftComboId,
                    [](const FTSGiftComboPayload&)
                    {
                    }
                ),
            "Expired GiftCombo must leave only Processing Chat"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "GiftCombo expiration must preserve Chat"
                    );
                }
            ) &&
                Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Chat must remain Processing after GiftCombo expires"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetGiftComboPayloadCount() == 0,
            "GiftCombo expiration scenario must clean Chat"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftComboPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "GiftCombo produces direct Gift/GiftCombo candidate",
            &TestGiftComboProducesDirectGiftComboCandidate
        });
        Tests.push_back({
            "GiftCombo payload snapshot and metadata independence",
            &TestGiftComboPayloadSnapshotAndMetadataIndependence
        });
    }

    void RegisterGiftComboPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "GiftCombo first admission Auto Pumps",
            &TestGiftComboFirstAdmissionAutoPumps
        });
        Tests.push_back({
            "GiftCombo second admission while busy",
            &TestGiftComboSecondAdmissionWhileBusy
        });
        Tests.push_back({
            "Disabled GiftCombo flow rejects without leaks",
            &TestDisabledGiftComboFlowRejectsWithoutLeaks
        });
        Tests.push_back({
            "GiftCombo capacity rejection removes provisional payload",
            &TestGiftComboCapacityRejectionRemovesProvisionalPayload
        });
        Tests.push_back({
            "GiftCombo dispatch owns snapshot and is one shot",
            &TestGiftComboDispatchOwnsSnapshotAndIsOneShot
        });
        Tests.push_back({
            "Wrong-route Begin preserves ready GiftCombo",
            &TestWrongRouteBeginPreservesReadyGiftCombo
        });
        Tests.push_back({
            "GiftCombo success completion confirms and cleans",
            &TestGiftComboSuccessCompletionConfirmsAndCleans
        });
        Tests.push_back({
            "GiftCombo cancel and failure clean terminal state",
            &TestGiftComboCancelAndFailureCleanTerminalState
        });
        Tests.push_back({
            "Wrong-route completion preserves GiftCombo InFlight",
            &TestWrongRouteCompletionPreservesGiftComboInFlight
        });
        Tests.push_back({
            "GiftCombo completion captures ready Gift",
            &TestGiftComboCompletionCapturesReadyGift
        });
        Tests.push_back({
            "Gift completion captures ready GiftCombo",
            &TestGiftCompletionCapturesReadyGiftCombo
        });
        Tests.push_back({
            "Pending GiftCombo expires while Chat is Processing",
            &TestPendingGiftComboExpiresWhileChatIsProcessing
        });
    }
}
