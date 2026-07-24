#include "EventPipeline/Families/TSLikeMilestoneFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>

namespace
{
    using namespace TikStudio::Tests;

    void MutateLikeInput(
        FTSLikeInput& Input,
        std::int32_t LikeCount,
        std::int32_t TotalLikeCount,
        const char* UniqueId,
        const char* Nickname,
        const char* ProfilePictureUrl,
        std::int32_t Offset
    )
    {
        Input.LikeCount = LikeCount;
        Input.TotalLikeCount = TotalLikeCount;
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

    void TestLikeMilestoneProducesDirectLikeMilestoneCandidate()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 37;
        Input.TotalLikeCount = 12345;

        const TTSFamilyDecision<FTSLikeMilestonePayload> Decision =
            FTSLikeMilestoneFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "LikeMilestone must produce a structural candidate"
        );
        Require(
            Decision->FamilyKind == ETSEventFamilyKind::Like &&
                Decision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone &&
                Decision->EnqueueRequest.Flow != ETSEventFlow::Like,
            "LikeMilestone candidate must use Like/LikeMilestone"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Like,
                ETSEventFlow::Like
            ),
            "Direct Like must remain operationally authorized"
        );
        Require(
            IsSupportedFamilyFlowPair(
                ETSEventFamilyKind::Like,
                ETSEventFlow::LikeMilestone
            ),
            "Like/LikeMilestone must be authorized in phase B"
        );
        Require(
            Decision->EnqueueRequest.PriorityAdjustment == 0 &&
                !Decision->EnqueueRequest.bOverrideTTL &&
                Decision->EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0} &&
                !Decision->EnqueueRequest.bProtectedFromEviction,
            "LikeMilestone must preserve neutral admission defaults"
        );
        RequireLikeInputEqual(
            Decision->Payload.Input,
            Input,
            "LikeMilestone structural payload"
        );
    }

    void TestLikeMilestonePayloadSnapshotAndCallIndependence()
    {
        FTSLikeInput FirstInput = MakeCompleteLikeInput();
        FirstInput.LikeCount = 41;
        FirstInput.TotalLikeCount = 9000;
        const FTSLikeInput FirstExpected = FirstInput;
        const TTSFamilyDecision<FTSLikeMilestonePayload> FirstDecision =
            FTSLikeMilestoneFamily::Decide(FirstInput);

        Require(
            FirstDecision.has_value(),
            "First LikeMilestone decision must produce a candidate"
        );
        RequireLikeInputEqual(
            FirstInput,
            FirstExpected,
            "First caller remains unchanged during Decide"
        );

        MutateLikeInput(
            FirstInput,
            101,
            15000,
            "first-mutated-like-user",
            "First Mutated Like User",
            "https://example.test/first-mutated-like.png",
            1
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First LikeMilestone payload remains an owned snapshot"
        );

        FTSLikeInput SecondInput = MakeCompleteLikeInput();
        MutateLikeInput(
            SecondInput,
            13,
            700,
            "second-like-user",
            "Second Like User",
            "https://example.test/second-like.png",
            2
        );
        SecondInput.User.bIsModerator = true;
        SecondInput.User.bIsSubscriber = false;
        SecondInput.User.bIsNewGifter = true;
        const FTSLikeInput SecondExpected = SecondInput;
        const TTSFamilyDecision<FTSLikeMilestonePayload> SecondDecision =
            FTSLikeMilestoneFamily::Decide(SecondInput);

        Require(
            SecondDecision.has_value() &&
                FirstDecision->FamilyKind == ETSEventFamilyKind::Like &&
                FirstDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone &&
                SecondDecision->FamilyKind == ETSEventFamilyKind::Like &&
                SecondDecision->EnqueueRequest.Flow ==
                    ETSEventFlow::LikeMilestone,
            "Independent decisions must select only Like/LikeMilestone"
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First decision remains independent"
        );
        RequireLikeInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second decision owns only its input"
        );

        MutateLikeInput(
            SecondInput,
            303,
            25000,
            "second-mutated-like-user",
            "Second Mutated Like User",
            "https://example.test/second-mutated-like.png",
            3
        );
        RequireLikeInputEqual(
            FirstDecision->Payload.Input,
            FirstExpected,
            "First payload remains independent after second caller mutation"
        );
        RequireLikeInputEqual(
            SecondDecision->Payload.Input,
            SecondExpected,
            "Second payload remains independent after caller mutation"
        );
    }

    void RequireLikeMilestoneBinding(
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
                                ETSEventFamilyKind::Like &&
                            Binding.ExpectedFlow ==
                                ETSEventFlow::LikeMilestone &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState == ExpectedState,
                        "LikeMilestone binding mismatch"
                    );
                }
            ),
            "LikeMilestone binding must exist"
        );
    }

    void RequireNoLikeAuthorities(
        const FTSEventPipelineCoordinator& Coordinator,
        const std::string& Context
    )
    {
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value() &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            Context
        );
    }

    void TestLikeMilestoneFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 37;
        Input.TotalLikeCount = 12345;
        const FTSLikeInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLikeMilestone(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First LikeMilestone admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow ==
                    ETSEventFlow::LikeMilestone &&
                CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First LikeMilestone admission envelope mismatch"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::LikeMilestone &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 1,
            "First LikeMilestone ready metadata mismatch"
        );
        RequireLikeMilestoneBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Bound
        );
        Require(
            Coordinator.VisitLikeMilestonePayloadForEmission(
                EmissionId,
                [&](const FTSLikeMilestonePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First LikeMilestone repository payload"
                    );
                }
            ),
            "First LikeMilestone payload must exist"
        );

        const FTSLikeMilestoneDispatchResult Dispatch =
            Coordinator.BeginLikeMilestoneProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value() &&
                Dispatch.Dispatch->Emission.EmissionId == EmissionId,
            "First LikeMilestone must dispatch"
        );
        RequireLikeInputEqual(
            Dispatch.Dispatch->Payload.Input,
            ExpectedInput,
            "First LikeMilestone dispatch payload"
        );
        const FTSLikeMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteLikeMilestoneProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed,
            "First LikeMilestone must confirm"
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "First LikeMilestone completion must clean authorities"
        );
    }

    void TestLikeMilestoneSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeLikeMilestoneSettings(true, 10)
        );
        FTSLikeInput FirstInput = MakeCompleteLikeInput();
        MutateLikeInput(
            FirstInput,
            17,
            1700,
            "first-like-milestone-user",
            "First Like Milestone User",
            "https://example.test/first-like-milestone.png",
            1
        );
        const FTSLikeInput ExpectedFirst = FirstInput;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitLikeMilestone(std::move(FirstInput));

        FTSLikeInput SecondInput = MakeCompleteLikeInput();
        MutateLikeInput(
            SecondInput,
            29,
            2900,
            "second-like-milestone-user",
            "Second Like Milestone User",
            "https://example.test/second-like-milestone.png",
            2
        );
        const FTSLikeInput ExpectedSecond = SecondInput;
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitLikeMilestone(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both LikeMilestone admissions must be accepted"
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
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::LikeMilestone,
            "Busy LikeMilestone admissions must preserve FIFO authorities"
        );
        Require(
            Coordinator.VisitLikeMilestonePayloadForEmission(
                FirstId,
                [&](const FTSLikeMilestonePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        ExpectedFirst,
                        "First busy LikeMilestone"
                    );
                }
            ) &&
                Coordinator.VisitLikeMilestonePayloadForEmission(
                    SecondId,
                    [&](const FTSLikeMilestonePayload& Payload)
                    {
                        RequireLikeInputEqual(
                            Payload.Input,
                            ExpectedSecond,
                            "Second busy LikeMilestone"
                        );
                    }
                ),
            "Both LikeMilestone snapshots must remain addressable"
        );

        Require(
            BeginReadyLikeMilestone(Coordinator) == FirstId,
            "First LikeMilestone must dispatch first"
        );
        RequireLikeMilestoneBinding(
            Coordinator,
            SecondId,
            ETSExternalEmissionState::Bound
        );
        const FTSLikeMilestoneProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteLikeMilestoneProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::LikeMilestone,
            "Completing first LikeMilestone must expose the second"
        );
        const FTSLikeMilestoneDispatchResult SecondDispatch =
            Coordinator.BeginLikeMilestoneProcessing();
        Require(
            SecondDispatch.Dispatch.has_value() &&
                SecondDispatch.Dispatch->Emission.EmissionId == SecondId,
            "Second LikeMilestone must preserve FIFO"
        );
        RequireLikeInputEqual(
            SecondDispatch.Dispatch->Payload.Input,
            ExpectedSecond,
            "Second LikeMilestone dispatch snapshot"
        );
        (void)Coordinator.CompleteLikeMilestoneProcessing(
            SecondId,
            ETSProcessingResult::Succeeded
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Busy LikeMilestone scenario must clean authorities"
        );
    }

    void TestDisabledLikeMilestoneFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeLikeMilestoneSettings(false, 1)
        );
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLikeMilestone(MakeCompleteLikeInput());

        Require(
            Admission.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled LikeMilestone must be rejected by Core"
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Disabled LikeMilestone must not leak authorities"
        );
    }

    void TestLikeMilestoneCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings =
            MakeLikeMilestoneSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitLikeMilestone(MakeCompleteLikeInput());
        FTSLikeInput SecondInput = MakeCompleteLikeInput();
        SecondInput.User.UniqueId = "capacity-rejected-like-milestone";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitLikeMilestone(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status ==
                    ETSPipelineAdmissionStatus::RejectedByCore &&
                Second.EnqueueResult.has_value() &&
                Second.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 1,
            "Capacity rejection must erase only provisional LikeMilestone"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.Pump().Outcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                BeginReadyLikeMilestone(Coordinator) == FirstId,
            "First LikeMilestone must remain operational after rejection"
        );
        (void)Coordinator.CompleteLikeMilestoneProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Capacity scenario must clean accepted LikeMilestone"
        );
    }

    void TestLikeMilestoneDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSLikeInput Input = MakeCompleteLikeInput();
        MutateLikeInput(
            Input,
            43,
            4300,
            "snapshot-like-milestone-user",
            "Snapshot Like Milestone User",
            "https://example.test/snapshot-like-milestone.png",
            4
        );
        const FTSLikeInput Expected = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLikeMilestone(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Snapshot LikeMilestone admission must succeed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;

        MutateLikeInput(
            Input,
            99,
            9900,
            "mutated-caller",
            "Mutated Caller",
            "https://example.test/mutated-caller.png",
            9
        );
        Require(
            Coordinator.VisitLikeMilestonePayloadForEmission(
                EmissionId,
                [&](const FTSLikeMilestonePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        Expected,
                        "Repository owns the admission snapshot"
                    );
                }
            ),
            "Snapshot LikeMilestone payload must exist"
        );

        FTSLikeMilestoneDispatchResult Dispatch =
            Coordinator.BeginLikeMilestoneProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "Snapshot LikeMilestone must dispatch"
        );
        RequireLikeInputEqual(
            Dispatch.Dispatch->Payload.Input,
            Expected,
            "Dispatch owns the repository snapshot"
        );
        MutateLikeInput(
            Dispatch.Dispatch->Payload.Input,
            111,
            11100,
            "mutated-dispatch",
            "Mutated Dispatch",
            "https://example.test/mutated-dispatch.png",
            11
        );
        Require(
            Coordinator.VisitLikeMilestonePayloadForEmission(
                EmissionId,
                [&](const FTSLikeMilestonePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        Expected,
                        "Dispatch mutation must not alter repository"
                    );
                }
            ),
            "Repository snapshot must remain after dispatch mutation"
        );

        const FTSLikeMilestoneDispatchResult SecondDispatch =
            Coordinator.BeginLikeMilestoneProcessing();
        Require(
            SecondDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value(),
            "LikeMilestone dispatch must be one shot"
        );
        RequireLikeMilestoneBinding(
            Coordinator,
            EmissionId,
            ETSExternalEmissionState::Processing
        );
        (void)Coordinator.CompleteLikeMilestoneProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongRouteBeginPreservesReadyLikeMilestone()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId = SubmitAcceptedLikeMilestone(
            Coordinator,
            "wrong-begin-like-milestone"
        );
        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                        ETSEventFamilyKind::Like &&
                    Coordinator.PeekPendingReadyFlow() ==
                        ETSEventFlow::LikeMilestone &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetLikePayloadCount() == 0 &&
                    Coordinator.GetLikeMilestonePayloadCount() == 1,
                "Wrong-route Begin must preserve LikeMilestone ready"
            );
            RequireLikeMilestoneBinding(
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
                "Wrong-route Begin must not dispatch LikeMilestone"
            );
            RequirePreserved();
        };

        RequireNoDispatch(Coordinator.BeginChatProcessing());
        RequireNoDispatch(Coordinator.BeginFollowProcessing());
        RequireNoDispatch(Coordinator.BeginShareProcessing());
        RequireNoDispatch(Coordinator.BeginShareMilestoneProcessing());
        RequireNoDispatch(Coordinator.BeginLikeProcessing());
        RequireNoDispatch(Coordinator.BeginRoomUserProcessing());
        RequireNoDispatch(Coordinator.BeginGiftProcessing());
        RequireNoDispatch(Coordinator.BeginGiftComboProcessing());
        RequireNoDispatch(Coordinator.BeginMemberProcessing());
        Require(
            BeginReadyLikeMilestone(Coordinator) == EmissionId,
            "Correct LikeMilestone Begin must consume preserved ready"
        );
        (void)Coordinator.CompleteLikeMilestoneProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestLikeMilestoneSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId = SubmitAcceptedLikeMilestone(
            Coordinator,
            "successful-like-milestone"
        );
        Require(
            BeginReadyLikeMilestone(Coordinator) == EmissionId,
            "Successful LikeMilestone must enter Processing"
        );
        const FTSLikeMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteLikeMilestoneProcessing(
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
            "Successful LikeMilestone lifecycle mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitLikeMilestonePayloadForEmission(
                    EmissionId,
                    [](const FTSLikeMilestonePayload&)
                    {
                    }
                ),
            "Successful LikeMilestone must remove binding and payload"
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Successful LikeMilestone completion must clean authorities"
        );
    }

    void TestLikeMilestoneCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminal = [](ETSProcessingResult ProcessingResult)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId EmissionId =
                SubmitAcceptedLikeMilestone(
                    Coordinator,
                    ProcessingResult == ETSProcessingResult::Cancelled
                        ? "cancelled-like-milestone"
                        : "failed-like-milestone"
                );
            Require(
                BeginReadyLikeMilestone(Coordinator) == EmissionId,
                "Terminal LikeMilestone must enter Processing"
            );
            const FTSLikeMilestoneProcessingCompletionResult Completion =
                Coordinator.CompleteLikeMilestoneProcessing(
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
                "Cancelled or Failed LikeMilestone terminal mismatch"
            );
            Coordinator.ValidateInternalConsistency();
            RequireNoLikeAuthorities(
                Coordinator,
                "Terminal LikeMilestone must clean without retry"
            );
        };

        RunTerminal(ETSProcessingResult::Cancelled);
        RunTerminal(ETSProcessingResult::Failed);
    }

    void TestWrongRouteCompletionPreservesLikeMilestoneInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 61;
        Input.TotalLikeCount = 6100;
        const FTSLikeInput Expected = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLikeMilestone(std::move(Input));
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Wrong-route LikeMilestone admission must succeed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            BeginReadyLikeMilestone(Coordinator) == EmissionId,
            "Wrong-route LikeMilestone must enter Processing"
        );

        const auto RequirePreserved = [&]()
        {
            RequireLikeMilestoneBinding(
                Coordinator,
                EmissionId,
                ETSExternalEmissionState::Processing
            );
            Require(
                Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetLikePayloadCount() == 0 &&
                    Coordinator.GetLikeMilestonePayloadCount() == 1 &&
                    !Coordinator.PeekPendingReadyFlow().has_value() &&
                    Coordinator.Pump().Outcome.Status ==
                        ETSPumpStatus::Busy,
                "Wrong completion must preserve LikeMilestone InFlight"
            );
            Require(
                Coordinator.VisitLikeMilestonePayloadForEmission(
                    EmissionId,
                    [&](const FTSLikeMilestonePayload& Payload)
                    {
                        RequireLikeInputEqual(
                            Payload.Input,
                            Expected,
                            "Wrong completion preserves LikeMilestone payload"
                        );
                    }
                ),
                "Wrong completion must preserve LikeMilestone repository"
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
            (void)Coordinator.CompleteShareMilestoneProcessing(
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

        const FTSLikeMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteLikeMilestoneProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value(),
            "Correct LikeMilestone completion must succeed"
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Correct LikeMilestone completion must clean authorities"
        );
    }

    void TestLikeMilestoneCompletionCapturesReadyLike()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId MilestoneId = SubmitAcceptedLikeMilestone(
            Coordinator,
            "milestone-before-like"
        );
        Require(
            BeginReadyLikeMilestone(Coordinator) == MilestoneId,
            "LikeMilestone must process before direct Like"
        );

        FTSLikeInput LikeInput = MakeCompleteLikeInput();
        LikeInput.LikeCount = 73;
        LikeInput.TotalLikeCount = 7300;
        const FTSLikeInput ExpectedLike = LikeInput;
        const FTSPipelineAdmissionResult LikeAdmission =
            Coordinator.SubmitLike(std::move(LikeInput));
        Require(
            LikeAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                LikeAdmission.EnqueueResult.has_value(),
            "Direct Like after LikeMilestone must be accepted"
        );
        const FTSEmissionId LikeId =
            LikeAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSLikeMilestoneProcessingCompletionResult Completion =
            Coordinator.CompleteLikeMilestoneProcessing(
                MilestoneId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == LikeId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::Like &&
                Coordinator.GetLikeMilestonePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 1,
            "LikeMilestone completion must expose direct Like"
        );
        const FTSLikeDispatchResult Dispatch =
            Coordinator.BeginLikeProcessing();
        Require(
            Dispatch.Dispatch.has_value() &&
                Dispatch.Dispatch->Emission.EmissionId == LikeId,
            "Direct Like must dispatch after LikeMilestone"
        );
        RequireLikeInputEqual(
            Dispatch.Dispatch->Payload.Input,
            ExpectedLike,
            "Direct Like preserves its own counters"
        );
        (void)Coordinator.CompleteLikeProcessing(
            LikeId,
            ETSProcessingResult::Succeeded
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "LikeMilestone then Like must clean authorities"
        );
    }

    void TestLikeCompletionCapturesReadyLikeMilestone()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSLikeInput LikeInput = MakeCompleteLikeInput();
        LikeInput.LikeCount = 83;
        LikeInput.TotalLikeCount = 8300;
        const FTSPipelineAdmissionResult LikeAdmission =
            Coordinator.SubmitLike(std::move(LikeInput));
        Require(
            LikeAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                LikeAdmission.EnqueueResult.has_value(),
            "Direct Like before LikeMilestone must be accepted"
        );
        const FTSEmissionId LikeId =
            LikeAdmission.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Direct Like must process before LikeMilestone"
        );

        FTSLikeInput MilestoneInput = MakeCompleteLikeInput();
        MilestoneInput.LikeCount = 97;
        MilestoneInput.TotalLikeCount = 9700;
        const FTSLikeInput ExpectedMilestone = MilestoneInput;
        const FTSPipelineAdmissionResult MilestoneAdmission =
            Coordinator.SubmitLikeMilestone(std::move(MilestoneInput));
        Require(
            MilestoneAdmission.Status ==
                    ETSPipelineAdmissionStatus::Accepted &&
                MilestoneAdmission.EnqueueResult.has_value(),
            "LikeMilestone after direct Like must be accepted"
        );
        const FTSEmissionId MilestoneId =
            MilestoneAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSLikeProcessingCompletionResult Completion =
            Coordinator.CompleteLikeProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == MilestoneId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like &&
                Coordinator.PeekPendingReadyFlow() ==
                    ETSEventFlow::LikeMilestone &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 1,
            "Like completion must expose LikeMilestone"
        );
        const FTSLikeMilestoneDispatchResult Dispatch =
            Coordinator.BeginLikeMilestoneProcessing();
        Require(
            Dispatch.Dispatch.has_value() &&
                Dispatch.Dispatch->Emission.EmissionId == MilestoneId,
            "LikeMilestone must dispatch after direct Like"
        );
        RequireLikeInputEqual(
            Dispatch.Dispatch->Payload.Input,
            ExpectedMilestone,
            "LikeMilestone preserves its own counters"
        );
        (void)Coordinator.CompleteLikeMilestoneProcessing(
            MilestoneId,
            ETSProcessingResult::Succeeded
        );
        RequireNoLikeAuthorities(
            Coordinator,
            "Like then LikeMilestone must clean authorities"
        );
    }

    void TestPendingLikeMilestoneExpiresWhileChatIsProcessing()
    {
        using namespace std::chrono_literals;

        FControlledClock Clock;
        FTSEventQueueSettings Settings =
            MakeOperationalLikeMilestoneSettings(
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
            "Processing while LikeMilestone expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must process before LikeMilestone"
        );
        const FTSEmissionId MilestoneId =
            SubmitAcceptedLikeMilestone(
                Coordinator,
                "expiring-like-milestone"
            );
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFlow().has_value(),
            "LikeMilestone must remain Pending while Chat processes"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    MilestoneId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::LikeMilestone &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending LikeMilestone expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    MilestoneId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.VisitLikeMilestonePayloadForEmission(
                    MilestoneId,
                    [](const FTSLikeMilestonePayload&)
                    {
                    }
                ),
            "Expired LikeMilestone must leave only Processing Chat"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "LikeMilestone expiration must preserve Chat"
                    );
                }
            ) &&
                Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Chat must remain Processing after LikeMilestone expires"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetLikeMilestonePayloadCount() == 0,
            "LikeMilestone expiration scenario must clean Chat"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikeMilestonePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "LikeMilestone produces direct Like/LikeMilestone candidate",
            &TestLikeMilestoneProducesDirectLikeMilestoneCandidate
        });
        Tests.push_back({
            "LikeMilestone payload snapshot and call independence",
            &TestLikeMilestonePayloadSnapshotAndCallIndependence
        });
    }

    void RegisterLikeMilestonePipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "LikeMilestone first admission Auto Pumps",
            &TestLikeMilestoneFirstAdmissionAutoPumps
        });
        Tests.push_back({
            "LikeMilestone second admission while busy",
            &TestLikeMilestoneSecondAdmissionWhileBusy
        });
        Tests.push_back({
            "Disabled LikeMilestone flow rejects without leaks",
            &TestDisabledLikeMilestoneFlowRejectsWithoutLeaks
        });
        Tests.push_back({
            "LikeMilestone capacity rejection removes provisional payload",
            &TestLikeMilestoneCapacityRejectionRemovesProvisionalPayload
        });
        Tests.push_back({
            "LikeMilestone dispatch owns snapshot and is one shot",
            &TestLikeMilestoneDispatchOwnsSnapshotAndIsOneShot
        });
        Tests.push_back({
            "Wrong-route Begin preserves ready LikeMilestone",
            &TestWrongRouteBeginPreservesReadyLikeMilestone
        });
        Tests.push_back({
            "LikeMilestone success completion confirms and cleans",
            &TestLikeMilestoneSuccessCompletionConfirmsAndCleans
        });
        Tests.push_back({
            "LikeMilestone cancel and failure clean terminal state",
            &TestLikeMilestoneCancelAndFailureCleanTerminalState
        });
        Tests.push_back({
            "Wrong-route completion preserves LikeMilestone InFlight",
            &TestWrongRouteCompletionPreservesLikeMilestoneInFlight
        });
        Tests.push_back({
            "LikeMilestone completion captures ready Like",
            &TestLikeMilestoneCompletionCapturesReadyLike
        });
        Tests.push_back({
            "Like completion captures ready LikeMilestone",
            &TestLikeCompletionCapturesReadyLikeMilestone
        });
        Tests.push_back({
            "Pending LikeMilestone expires while Chat is Processing",
            &TestPendingLikeMilestoneExpiresWhileChatIsProcessing
        });
    }
}
