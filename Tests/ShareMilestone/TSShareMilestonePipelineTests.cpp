#include "EventPipeline/Families/TSShareMilestoneFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void MutateShareInput(
        FTSShareInput& Input,
        const char* UniqueId,
        const char* Nickname,
        const char* ProfilePictureUrl,
        std::int32_t Offset
    )
    {
        Input.User.UniqueId = UniqueId;
        Input.User.Nickname = Nickname;
        Input.User.ProfilePictureUrl = ProfilePictureUrl;
        Input.User.FollowRole = 20 + Offset;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 30 + Offset;
        Input.User.GifterLevel = 40 + Offset;
        Input.User.TeamMemberLevel = 50 + Offset;
    }

    void TestShareMilestoneProducesDirectShareMilestoneCandidate()
    {
        const FTSShareInput Input = MakeCompleteShareInput();
        const TTSFamilyDecision<FTSShareMilestonePayload> Decision =
            FTSShareMilestoneFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "ShareMilestone must produce a structural candidate"
        );
        Require(
            Decision->FamilyKind == ETSEventFamilyKind::Share &&
                Decision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone &&
                Decision->EnqueueRequest.Flow != ETSEventFlow::Share,
            "ShareMilestone candidate must use Share/ShareMilestone"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Share,
                ETSEventFlow::Share
            ),
            "Direct Share must remain operationally authorized"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Share,
                ETSEventFlow::ShareMilestone
            ),
            "Share/ShareMilestone must be authorized in phase B"
        );
        Require(
            Decision->EnqueueRequest.PriorityAdjustment == 0 &&
                !Decision->EnqueueRequest.bOverrideTTL &&
                Decision->EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0} &&
                !Decision->EnqueueRequest.bProtectedFromEviction,
            "ShareMilestone must preserve neutral admission defaults"
        );
        RequireShareInputEqual(
            Decision->Payload.Input,
            Input,
            "ShareMilestone structural payload"
        );
    }

    void TestShareMilestonePayloadSnapshotAndCallIndependence()
    {
        FTSShareInput FirstInput = MakeCompleteShareInput();
        const FTSShareInput FirstExpected = FirstInput;
        const TTSFamilyDecision<FTSShareMilestonePayload> FirstDecision =
            FTSShareMilestoneFamily::Decide(FirstInput);

        Require(
            FirstDecision.has_value(),
            "First ShareMilestone decision must produce a candidate"
        );
        RequireShareInputEqual(
            FirstInput,
            FirstExpected,
            "First caller remains unchanged during Decide"
        );

        MutateShareInput(
            FirstInput,
            "first-mutated-user",
            "First Mutated User",
            "https://example.test/first-mutated.png",
            1
        );
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First ShareMilestone payload remains an owned snapshot"
        );

        FTSShareInput SecondInput = MakeCompleteShareInput();
        MutateShareInput(
            SecondInput,
            "second-user",
            "Second User",
            "https://example.test/second.png",
            2
        );
        const FTSShareInput SecondExpected = SecondInput;
        const TTSFamilyDecision<FTSShareMilestonePayload> SecondDecision =
            FTSShareMilestoneFamily::Decide(SecondInput);

        Require(
            SecondDecision.has_value() &&
                FirstDecision->FamilyKind == ETSEventFamilyKind::Share &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Share &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::ShareMilestone,
            "Independent decisions must select only Share/ShareMilestone"
        );
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First decision remains independent"
        );
        RequireShareInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second decision owns only its input"
        );

        MutateShareInput(
            SecondInput,
            "second-mutated-user",
            "Second Mutated User",
            "https://example.test/second-mutated.png",
            3
        );
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        RequireShareInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First payload remains independent after second caller mutation"
        );
        RequireShareInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second payload remains independent after caller mutation"
        );
    }

    void RequireShareMilestoneBinding(
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
                            Binding.FamilyKind ==
                                ETSEventFamilyKind::Share &&
                            Binding.ExpectedFlow ==
                                ETSEventFlow::ShareMilestone &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState == ExpectedState,
                        "ShareMilestone binding mismatch"
                    );
                }
            ),
            "ShareMilestone binding must exist"
        );
    }

    void RequireNoShareAuthorities(
        const FTSEventPipelineCoordinator& Coordinator,
        const std::string& Context
    )
    {
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value() &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            Context
        );
    }

    void TestShareMilestoneFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSShareInput ExpectedInput = MakeCompleteShareInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShareMilestone(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First ShareMilestone admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow ==
                    ETSEventFlow::ShareMilestone &&
                CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First ShareMilestone admission envelope mismatch"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::ShareMilestone &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 1,
            "First ShareMilestone ready metadata mismatch"
        );
        RequireShareMilestoneBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Bound
        );
        Require(
            Coordinator.VisitShareMilestonePayloadForEmission(
                EmissionId,
                [&](const FTSShareMilestonePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First ShareMilestone payload"
                    );
                }
            ),
            "First ShareMilestone payload must exist"
        );
        Coordinator.ValidateInternalConsistency();

        Require(
            BeginReadyShareMilestone(Coordinator) == EmissionId,
            "First ShareMilestone must enter Processing"
        );
        const FTSShareMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteShareMilestoneProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed,
            "First ShareMilestone must confirm"
        );
        RequireNoShareAuthorities(
            Coordinator,
            "First ShareMilestone completion must clean authorities"
        );
    }

    void TestShareMilestoneSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeShareMilestoneSettings(true, 10)
        );
        FTSShareInput FirstInput = MakeCompleteShareInput();
        FirstInput.User.UniqueId = "first-milestone-user";
        FirstInput.User.Nickname = "First Milestone User";
        const FTSShareInput ExpectedFirst = FirstInput;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitShareMilestone(std::move(FirstInput));

        FTSShareInput SecondInput = MakeCompleteShareInput();
        SecondInput.User.UniqueId = "second-milestone-user";
        SecondInput.User.Nickname = "Second Milestone User";
        const FTSShareInput ExpectedSecond = SecondInput;
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitShareMilestone(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both ShareMilestone admissions must be accepted"
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
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::ShareMilestone,
            "Busy ShareMilestone admissions must preserve FIFO authorities"
        );
        Require(
            Coordinator.VisitShareMilestonePayloadForEmission(
                FirstId,
                [&](const FTSShareMilestonePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        ExpectedFirst,
                        "First busy ShareMilestone"
                    );
                }
            ) &&
                Coordinator.VisitShareMilestonePayloadForEmission(
                    SecondId,
                    [&](const FTSShareMilestonePayload& Payload)
                    {
                        RequireShareInputEqual(
                            Payload.Input,
                            ExpectedSecond,
                            "Second busy ShareMilestone"
                        );
                    }
                ),
            "Busy ShareMilestone snapshots must remain distinct"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == FirstId,
            "First ShareMilestone must preserve FIFO order"
        );
        const FTSShareMilestoneProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteShareMilestoneProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::ShareMilestone &&
                Coordinator.GetShareMilestonePayloadCount() == 1,
            "First completion must expose the second ShareMilestone"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == SecondId,
            "Second ShareMilestone must become ready"
        );
        (void)Coordinator.CompleteShareMilestoneProcessing(
            SecondId,
            ETSProcessingResult::Succeeded
        );
        RequireNoShareAuthorities(
            Coordinator,
            "Busy ShareMilestone scenario must clean authorities"
        );
    }

    void TestDisabledShareMilestoneFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeShareMilestoneSettings(false, 1)
        );
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShareMilestone(MakeCompleteShareInput());
        Require(
            Admission.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled ShareMilestone must be rejected"
        );
        RequireNoShareAuthorities(
            Coordinator,
            "Disabled ShareMilestone must not leak authorities"
        );
    }

    void TestShareMilestoneCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings =
            MakeShareMilestoneSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSShareInput FirstInput = MakeCompleteShareInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitShareMilestone(FirstInput);
        FTSShareInput RejectedInput = MakeCompleteShareInput();
        RejectedInput.User.UniqueId = "rejected-milestone-user";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitShareMilestone(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Rejected.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 1,
            "ShareMilestone capacity rejection must remove provisional payload"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.VisitShareMilestonePayloadForEmission(
                FirstId,
                [&](const FTSShareMilestonePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity ShareMilestone"
                    );
                }
            ),
            "First capacity ShareMilestone must remain"
        );
        Require(
            Coordinator.Pump().Outcome.Status ==
                ETSPumpStatus::EmissionReady,
            "Preserved ShareMilestone must become ready"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == FirstId,
            "Preserved ShareMilestone must dispatch"
        );
        (void)Coordinator.CompleteShareMilestoneProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        RequireNoShareAuthorities(
            Coordinator,
            "ShareMilestone capacity scenario must clean authorities"
        );
    }

    void TestShareMilestoneDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSShareInput Input = MakeCompleteShareInput();
        const FTSShareInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitShareMilestone(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned ShareMilestone admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        Input.User.UniqueId = "mutated-caller-user";
        Input.User.Nickname = "Mutated Caller";

        FTSShareMilestoneDispatchResult Dispatch =
            Coordinator.BeginShareMilestoneProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value() &&
                Dispatch.Dispatch->Emission.EmissionId == EmissionId &&
                Dispatch.Dispatch->Emission.Flow ==
                    ETSEventFlow::ShareMilestone,
            "ShareMilestone ready must dispatch once"
        );
        RequireShareInputEqual(
            Dispatch.Dispatch->Payload.Input,
            ExpectedInput,
            "Owned ShareMilestone dispatch"
        );
        Dispatch.Dispatch->Payload.Input.User.UniqueId =
            "mutated-dispatch-user";
        Require(
            Coordinator.VisitShareMilestonePayloadForEmission(
                EmissionId,
                [&](const FTSShareMilestonePayload& Payload)
                {
                    RequireShareInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "Stored ShareMilestone after dispatch mutation"
                    );
                }
            ),
            "Stored ShareMilestone must remain during Processing"
        );
        const FTSShareMilestoneDispatchResult Second =
            Coordinator.BeginShareMilestoneProcessing();
        Require(
            Second.Status == ETSPipelineDispatchStatus::NoEmissionReady &&
                !Second.Dispatch.has_value() &&
                Coordinator.GetShareMilestonePayloadCount() == 1,
            "ShareMilestone dispatch must consume ready exactly once"
        );
        RequireShareMilestoneBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Processing
        );
        (void)Coordinator.CompleteShareMilestoneProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongRouteBeginPreservesReadyShareMilestone()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId = SubmitAcceptedShareMilestone(
            Coordinator,
            "wrong-begin-milestone-user"
        );
        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                        ETSEventFamilyKind::Share &&
                    Coordinator.PeekPendingReadyFlow() ==
                        ETSEventFlow::ShareMilestone &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetSharePayloadCount() == 0 &&
                    Coordinator.GetShareMilestonePayloadCount() == 1,
                "Wrong-route Begin must preserve ShareMilestone ready"
            );
            RequireShareMilestoneBinding(
                Coordinator,
                EmissionId,
                ETSExternalEmissionState::Bound
            );
        };
        const auto RequireNoDispatch = [&](const auto& Result)
        {
            Require(
                Result.Status ==
                        ETSPipelineDispatchStatus::NoEmissionReady &&
                    !Result.Dispatch.has_value(),
                "Wrong-route Begin must not dispatch ShareMilestone"
            );
            RequirePreserved();
        };

        RequireNoDispatch(Coordinator.BeginChatProcessing());
        RequireNoDispatch(Coordinator.BeginFollowProcessing());
        RequireNoDispatch(Coordinator.BeginShareProcessing());
        RequireNoDispatch(Coordinator.BeginLikeProcessing());
        RequireNoDispatch(Coordinator.BeginRoomUserProcessing());
        RequireNoDispatch(Coordinator.BeginGiftProcessing());
        RequireNoDispatch(Coordinator.BeginGiftComboProcessing());
        RequireNoDispatch(Coordinator.BeginMemberProcessing());
        Require(
            BeginReadyShareMilestone(Coordinator) == EmissionId,
            "Correct ShareMilestone Begin must consume preserved ready"
        );
        (void)Coordinator.CompleteShareMilestoneProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestShareMilestoneSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId = SubmitAcceptedShareMilestone(
            Coordinator,
            "successful-milestone-user"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == EmissionId,
            "Successful ShareMilestone must enter Processing"
        );
        const FTSShareMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteShareMilestoneProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult ==
                    ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful ShareMilestone lifecycle mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitShareMilestonePayloadForEmission(
                    EmissionId,
                    [](const FTSShareMilestonePayload&)
                    {
                    }
                ),
            "Successful ShareMilestone must remove binding and payload"
        );
        RequireNoShareAuthorities(
            Coordinator,
            "Successful ShareMilestone completion must clean authorities"
        );
    }

    void TestShareMilestoneCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminal = [](ETSProcessingResult ProcessingResult)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId EmissionId =
                SubmitAcceptedShareMilestone(
                    Coordinator,
                    ProcessingResult == ETSProcessingResult::Cancelled
                        ? "cancelled-milestone-user"
                        : "failed-milestone-user"
                );
            Require(
                BeginReadyShareMilestone(Coordinator) == EmissionId,
                "Terminal ShareMilestone must enter Processing"
            );
            const FTSShareMilestoneProcessingCompletionResult Completion =
                Coordinator.CompleteShareMilestoneProcessing(
                    EmissionId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == EmissionId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value() &&
                    Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == EmissionId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed ShareMilestone terminal mismatch"
            );
            RequireNoShareAuthorities(
                Coordinator,
                "Terminal ShareMilestone must clean without retry"
            );
        };

        RunTerminal(ETSProcessingResult::Cancelled);
        RunTerminal(ETSProcessingResult::Failed);
    }

    void TestWrongRouteCompletionPreservesShareMilestoneInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId = SubmitAcceptedShareMilestone(
            Coordinator,
            "wrong-completion-milestone-user"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == EmissionId,
            "Wrong-route ShareMilestone must enter Processing"
        );
        const auto RequirePreserved = [&]()
        {
            RequireShareMilestoneBinding(
                Coordinator,
                EmissionId,
                ETSExternalEmissionState::Processing
            );
            Require(
                Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetSharePayloadCount() == 0 &&
                    Coordinator.GetShareMilestonePayloadCount() == 1 &&
                    Coordinator.Pump().Outcome.Status ==
                        ETSPumpStatus::Busy,
                "Wrong completion must preserve ShareMilestone InFlight"
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
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteFollowProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteShareProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteLikeProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteRoomUserProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteGiftProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteGiftComboProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireRejected([&]()
        {
            (void)Coordinator.CompleteMemberProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        });

        const FTSShareMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteShareMilestoneProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value(),
            "Correct ShareMilestone completion must succeed"
        );
        RequireNoShareAuthorities(
            Coordinator,
            "Correct ShareMilestone completion must clean authorities"
        );
    }

    void TestShareMilestoneCompletionCapturesReadyShare()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId MilestoneId = SubmitAcceptedShareMilestone(
            Coordinator,
            "milestone-before-share-user"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == MilestoneId,
            "ShareMilestone must process before Share"
        );
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "share-after-milestone-user");
        const FTSShareMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteShareMilestoneProcessing(
                MilestoneId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == ShareId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::Share &&
                Coordinator.GetShareMilestonePayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 1,
            "ShareMilestone completion must expose direct Share"
        );
        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Direct Share must dispatch after ShareMilestone"
        );
        (void)Coordinator.CompleteShareProcessing(
            ShareId,
            ETSProcessingResult::Succeeded
        );
        RequireNoShareAuthorities(
            Coordinator,
            "ShareMilestone then Share must clean authorities"
        );
    }

    void TestShareCompletionCapturesReadyShareMilestone()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "share-before-milestone-user");
        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Share must process before ShareMilestone"
        );
        const FTSEmissionId MilestoneId = SubmitAcceptedShareMilestone(
            Coordinator,
            "milestone-after-share-user"
        );
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
                        .EmissionId == MilestoneId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Share &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::ShareMilestone &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 1,
            "Share completion must expose ShareMilestone"
        );
        Require(
            BeginReadyShareMilestone(Coordinator) == MilestoneId,
            "ShareMilestone must dispatch after direct Share"
        );
        (void)Coordinator.CompleteShareMilestoneProcessing(
            MilestoneId,
            ETSProcessingResult::Succeeded
        );
        RequireNoShareAuthorities(
            Coordinator,
            "Share then ShareMilestone must clean authorities"
        );
    }

    void TestPendingShareMilestoneExpiresWhileChatIsProcessing()
    {
        using namespace std::chrono_literals;

        FControlledClock Clock;
        FTSEventQueueSettings Settings =
            MakeOperationalShareMilestoneSettings(
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
            "Processing while ShareMilestone expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must process before ShareMilestone"
        );
        const FTSEmissionId MilestoneId = SubmitAcceptedShareMilestone(
            Coordinator,
            "expiring-milestone-user"
        );
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            "ShareMilestone must remain Pending while Chat processes"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    MilestoneId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::ShareMilestone &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending ShareMilestone expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    MilestoneId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.VisitShareMilestonePayloadForEmission(
                    MilestoneId,
                    [](const FTSShareMilestonePayload&)
                    {
                    }
                ),
            "Expired ShareMilestone must leave only Processing Chat"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "ShareMilestone expiration must preserve Chat"
                    );
                }
            ) &&
                Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Chat must remain Processing after ShareMilestone expires"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetShareMilestonePayloadCount() == 0,
            "ShareMilestone expiration scenario must clean Chat"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterShareMilestonePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "ShareMilestone produces direct Share/ShareMilestone candidate",
            &TestShareMilestoneProducesDirectShareMilestoneCandidate
        });
        Tests.push_back({
            "ShareMilestone payload snapshot and call independence",
            &TestShareMilestonePayloadSnapshotAndCallIndependence
        });
    }

    void RegisterShareMilestonePipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "ShareMilestone first admission Auto Pumps",
            &TestShareMilestoneFirstAdmissionAutoPumps
        });
        Tests.push_back({
            "ShareMilestone second admission while busy",
            &TestShareMilestoneSecondAdmissionWhileBusy
        });
        Tests.push_back({
            "Disabled ShareMilestone flow rejects without leaks",
            &TestDisabledShareMilestoneFlowRejectsWithoutLeaks
        });
        Tests.push_back({
            "ShareMilestone capacity rejection removes provisional payload",
            &TestShareMilestoneCapacityRejectionRemovesProvisionalPayload
        });
        Tests.push_back({
            "ShareMilestone dispatch owns snapshot and is one shot",
            &TestShareMilestoneDispatchOwnsSnapshotAndIsOneShot
        });
        Tests.push_back({
            "Wrong-route Begin preserves ready ShareMilestone",
            &TestWrongRouteBeginPreservesReadyShareMilestone
        });
        Tests.push_back({
            "ShareMilestone success completion confirms and cleans",
            &TestShareMilestoneSuccessCompletionConfirmsAndCleans
        });
        Tests.push_back({
            "ShareMilestone cancel and failure clean terminal state",
            &TestShareMilestoneCancelAndFailureCleanTerminalState
        });
        Tests.push_back({
            "Wrong-route completion preserves ShareMilestone InFlight",
            &TestWrongRouteCompletionPreservesShareMilestoneInFlight
        });
        Tests.push_back({
            "ShareMilestone completion captures ready Share",
            &TestShareMilestoneCompletionCapturesReadyShare
        });
        Tests.push_back({
            "Share completion captures ready ShareMilestone",
            &TestShareCompletionCapturesReadyShareMilestone
        });
        Tests.push_back({
            "Pending ShareMilestone expires while Chat is Processing",
            &TestPendingShareMilestoneExpiresWhileChatIsProcessing
        });
    }
}
