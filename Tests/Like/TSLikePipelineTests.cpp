#include "EventPipeline/Families/TSLikeFamily.h"
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

    void TestLikeCandidateAndAdmissionDefaults()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        Input.LikeCount = 250;
        Input.TotalLikeCount = 500000;

        const TTSFamilyDecision<FTSLikePayload> Decision =
            FTSLikeFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Like must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSLikePayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Like,
            "Like candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Like,
            "Like candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::LikeUser,
            "Like counters must not select LikeUser"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Like counters must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Like candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "Like candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Like candidate must not request eviction protection"
        );
    }

    void TestLikePayloadSnapshotAndInputPreservation()
    {
        FTSLikeInput Input = MakeCompleteLikeInput();
        const FTSLikeInput Original = Input;

        const TTSFamilyDecision<FTSLikePayload> Decision =
            FTSLikeFamily::Decide(Input);

        Require(Decision.has_value(), "Like snapshot must produce a candidate");
        RequireLikeInputEqual(
            Input,
            Original,
            "Like input after by-copy decision"
        );

        Input.LikeCount = 0;
        Input.TotalLikeCount = 0;
        Input.User.UniqueId = "mutated-like-user";
        Input.User.Nickname = "Mutated Like User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireLikeInputEqual(
            Decision->Payload.Input,
            Original,
            "Like payload snapshot"
        );
    }

    void TestLikeFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSLikeInput ExpectedInput = MakeCompleteLikeInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLike(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First Like admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow == ETSEventFlow::Like,
            "First Like admission envelope mismatch"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First Like admission must Auto Pump"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 1,
            "First Like admission authority counts mismatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [EmissionId](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.EmissionId == EmissionId &&
                            Binding.FamilyKind == ETSEventFamilyKind::Like &&
                            Binding.ExpectedFlow == ETSEventFlow::Like &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "First Like binding mismatch"
                    );
                }
            ),
            "First Like binding must exist"
        );
        Require(
            Coordinator.VisitLikePayloadForEmission(
                EmissionId,
                [&](const FTSLikePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First Like payload"
                    );
                }
            ),
            "First Like payload must exist"
        );

        Require(
            BeginReadyLike(Coordinator) == EmissionId,
            "First Like must enter Processing"
        );
        const FTSLikeProcessingCompletionResult Completion =
            Coordinator.CompleteLikeProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0,
            "First Like completion must confirm and clean authorities"
        );
    }

    void TestLikeSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(MakeLikeSettings(true, 10));
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitLike(MakeCompleteLikeInput());
        FTSLikeInput SecondInput = MakeCompleteLikeInput();
        SecondInput.User.UniqueId = "second-like-user";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitLike(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both Like admissions must be accepted"
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
                Coordinator.GetLikePayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like,
            "Busy Like admissions must preserve distinct authorities"
        );

        Require(
            BeginReadyLike(Coordinator) == FirstId,
            "Busy Like admission must preserve the first ready"
        );
        const FTSLikeProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteLikeProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == SecondId &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like,
            "First Like confirmation must expose the second ready"
        );

        Require(
            BeginReadyLike(Coordinator) == SecondId,
            "Second Like must become dispatchable"
        );
        const FTSLikeProcessingCompletionResult SecondCompletion =
            Coordinator.CompleteLikeProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        Require(
            SecondCompletion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Both Like admissions must finish without retained authorities"
        );
    }

    void TestDisabledLikeFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(MakeLikeSettings(false, 1));
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLike(MakeCompleteLikeInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled Like flow must be rejected by the core"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled Like rejection must remove its provisional payload"
        );
    }

    void TestLikeCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings = MakeLikeSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSLikeInput FirstInput = MakeCompleteLikeInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitLike(FirstInput);
        FTSLikeInput RejectedInput = MakeCompleteLikeInput();
        RejectedInput.User.UniqueId = "rejected-like-user";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitLike(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity Like admission must succeed"
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second Like admission must reject at capacity"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 1,
            "Capacity rejection must preserve only the prior Like"
        );
        Require(
            Coordinator.VisitLikePayloadForEmission(
                FirstId,
                [&](const FTSLikePayload& Payload)
                {
                    RequireLikeInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity Like"
                    );
                }
            ),
            "Prior Like payload must remain available"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == FirstId,
            "Explicit Pump must select the preserved Like"
        );
        Require(
            BeginReadyLike(Coordinator) == FirstId,
            "Preserved Like must enter Processing"
        );
        (void)Coordinator.CompleteLikeProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0,
            "Capacity scenario must finish without retained authorities"
        );
    }

    void TestLikeDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSLikeInput Input = MakeCompleteLikeInput();
        const FTSLikeInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitLike(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned Like dispatch admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;

        Input.LikeCount = 0;
        Input.TotalLikeCount = 0;
        Input.User.UniqueId = "mutated-caller-like-user";
        Input.User.Nickname = "Mutated caller Like";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        FTSLikeDispatchResult DispatchResult =
            Coordinator.BeginLikeProcessing();
        Require(
            DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched &&
                DispatchResult.Dispatch.has_value(),
            "Authorized Like ready must dispatch"
        );
        Require(
            DispatchResult.Dispatch->Emission.EmissionId == EmissionId &&
                DispatchResult.Dispatch->Emission.Flow == ETSEventFlow::Like,
            "Like dispatch envelope mismatch"
        );
        RequireLikeInputEqual(
            DispatchResult.Dispatch->Payload.Input,
            ExpectedInput,
            "Like dispatch payload"
        );

        FTSLikeInput& DispatchInput = DispatchResult.Dispatch->Payload.Input;
        DispatchInput.LikeCount = 0;
        DispatchInput.TotalLikeCount = 0;
        DispatchInput.User.UniqueId = "mutated-dispatch-like-user";
        DispatchInput.User.Nickname = "Mutated Like dispatch copy";
        DispatchInput.User.ProfilePictureUrl.clear();
        DispatchInput.User.FollowRole = 0;
        DispatchInput.User.bIsModerator = false;
        DispatchInput.User.bIsSubscriber = true;
        DispatchInput.User.bIsNewGifter = false;
        DispatchInput.User.TopGifterRank = 0;
        DispatchInput.User.GifterLevel = 0;
        DispatchInput.User.TeamMemberLevel = 0;

        Require(
            Coordinator.VisitLikePayloadForEmission(
                EmissionId,
                [&](const FTSLikePayload& StoredPayload)
                {
                    RequireLikeInputEqual(
                        StoredPayload.Input,
                        ExpectedInput,
                        "Stored Like after dispatch mutation"
                    );
                }
            ),
            "Stored Like must remain after dispatch mutation"
        );
        const FTSLikeDispatchResult SecondDispatch =
            Coordinator.BeginLikeProcessing();
        Require(
            SecondDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value() &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Like dispatch must consume its ready exactly once"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Like dispatch must authorize Processing"
                    );
                }
            ) && Coordinator.GetLikePayloadCount() == 1,
            "Dispatched Like authorities must remain until completion"
        );
        (void)Coordinator.CompleteLikeProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongFamilyBeginPreservesReadyLike()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "wrong-begin-like-user");

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        const FTSFollowDispatchResult FollowDispatch =
            Coordinator.BeginFollowProcessing();
        const FTSShareDispatchResult ShareDispatch =
            Coordinator.BeginShareProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::NoEmissionReady &&
                !ChatDispatch.Dispatch.has_value() &&
                FollowDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !FollowDispatch.Dispatch.has_value() &&
                ShareDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !ShareDispatch.Dispatch.has_value(),
            "Wrong-family Begins must not dispatch a Like ready"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 1,
            "Wrong-family Begins must preserve Like authorities"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                LikeId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Wrong-family Begins must preserve Like Bound"
                    );
                }
            ),
            "Wrong-family Begins must preserve the Like binding"
        );

        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Like Begin must dispatch the preserved ready"
        );
        (void)Coordinator.CompleteLikeProcessing(
            LikeId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestLikeSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "successful-like-user");
        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Successful Like must enter Processing"
        );

        const FTSLikeProcessingCompletionResult Completion =
            Coordinator.CompleteLikeProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == LikeId &&
                Completion.ProcessingResult == ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Successful Like completion result mismatch"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == LikeId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful Like completion terminal mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                LikeId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitLikePayloadForEmission(
                    LikeId,
                    [](const FTSLikePayload&)
                    {
                    }
                ) &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Successful Like completion must clean terminal authorities"
        );
    }

    void TestLikeCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminalScenario = [](ETSProcessingResult ProcessingResult)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId LikeId = SubmitAcceptedLike(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-like-user"
                    : "failed-like-user"
            );
            Require(
                BeginReadyLike(Coordinator) == LikeId,
                "Terminal Like must enter Processing"
            );
            const FTSLikeProcessingCompletionResult Completion =
                Coordinator.CompleteLikeProcessing(LikeId, ProcessingResult);
            Require(
                Completion.EmissionId == LikeId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed Like must expose only CancelResult"
            );
            Require(
                Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == LikeId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed Like must use cancellation terminal"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetLikePayloadCount() == 0 &&
                    !Coordinator.PeekPendingReadyFamilyKind().has_value(),
                "Terminal Like must clean authorities without retry"
            );
        };

        RunTerminalScenario(ETSProcessingResult::Cancelled);
        RunTerminalScenario(ETSProcessingResult::Failed);
    }

    void TestWrongFamilyCompletionPreservesLikeInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "wrong-completion-like-user");
        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Wrong-family completion Like must enter Processing"
        );

        bool bChatThrew = false;
        try
        {
            (void)Coordinator.CompleteChatProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bChatThrew = true;
        }
        bool bFollowThrew = false;
        try
        {
            (void)Coordinator.CompleteFollowProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bFollowThrew = true;
        }
        bool bShareThrew = false;
        try
        {
            (void)Coordinator.CompleteShareProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bShareThrew = true;
        }
        Require(
            bChatThrew && bFollowThrew && bShareThrew,
            "Existing-family completions must reject a Like binding"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                LikeId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Like &&
                            Binding.ExpectedFlow == ETSEventFlow::Like &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Processing,
                        "Wrong completion must preserve Like Processing"
                    );
                }
            ) &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 1,
            "Wrong completion must preserve Like authorities"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with the Like InFlight"
        );

        const FTSLikeProcessingCompletionResult Completion =
            Coordinator.CompleteLikeProcessing(
                LikeId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0,
            "Correct Like completion must clean preserved authorities"
        );
    }

    void TestLikeCompletionCapturesReadyChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "like-before-chat-user");
        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Like must enter Processing before Chat admission"
        );

        FTSChatInput ExpectedChat = MakeCompleteInput();
        ExpectedChat.Comment = "Chat after Like";
        const FTSPipelineAdmissionResult ChatAdmission =
            Coordinator.SubmitChat(ExpectedChat);
        Require(
            ChatAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ChatAdmission.EnqueueResult.has_value(),
            "Chat after Like must be accepted"
        );
        const FTSEmissionId ChatId =
            ChatAdmission.EnqueueResult->AdmittedEmission.EmissionId;

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
                        .EmissionId == ChatId &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat,
            "Like confirmation must expose the pending Chat"
        );

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ChatDispatch.Dispatch.has_value() &&
                ChatDispatch.Dispatch->Emission.EmissionId == ChatId,
            "Chat selected after Like must dispatch"
        );
        RequireChatInputEqual(
            ChatDispatch.Dispatch->Payload.Input,
            ExpectedChat,
            "Chat after Like payload"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestShareCompletionCapturesReadyLike()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ShareId =
            SubmitAcceptedShare(Coordinator, "share-before-like-user");
        Require(
            BeginReadyShare(Coordinator) == ShareId,
            "Share must enter Processing before Like admission"
        );

        FTSLikeInput ExpectedLike = MakeCompleteLikeInput();
        ExpectedLike.User.UniqueId = "like-after-share-user";
        const FTSPipelineAdmissionResult LikeAdmission =
            Coordinator.SubmitLike(ExpectedLike);
        Require(
            LikeAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                LikeAdmission.EnqueueResult.has_value(),
            "Like after Share must be accepted"
        );
        const FTSEmissionId LikeId =
            LikeAdmission.EnqueueResult->AdmittedEmission.EmissionId;

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
                        .EmissionId == LikeId &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Like,
            "Share confirmation must expose the pending Like"
        );

        const FTSLikeDispatchResult LikeDispatch =
            Coordinator.BeginLikeProcessing();
        Require(
            LikeDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                LikeDispatch.Dispatch.has_value() &&
                LikeDispatch.Dispatch->Emission.EmissionId == LikeId &&
                LikeDispatch.Dispatch->Emission.Flow == ETSEventFlow::Like,
            "Like selected after Share must dispatch"
        );
        RequireLikeInputEqual(
            LikeDispatch.Dispatch->Payload.Input,
            ExpectedLike,
            "Like after Share payload"
        );
        (void)Coordinator.CompleteLikeProcessing(
            LikeId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestPendingLikeExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalLikeSettings(
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
        ChatSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;

        FTSEventPipelineCoordinator Coordinator(
            std::move(Settings),
            Clock.MakeProvider()
        );
        const FTSEmissionId ChatId =
            SubmitAcceptedChat(Coordinator, "Processing while Like expires");
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Like admission"
        );
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "expiring-like-user");

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    LikeId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::Like &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Like expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    LikeId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Expired Like must leave only Processing Chat authorities"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Like expiration must preserve Chat Processing"
                    );
                }
            ),
            "Processing Chat binding must remain available"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Chat after Like expiration"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterLikePipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Like candidate and admission defaults",
            &TestLikeCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "Like payload snapshot and input preservation",
            &TestLikePayloadSnapshotAndInputPreservation
        });
    }

    void RegisterLikePipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Like first admission Auto Pumps", &TestLikeFirstAdmissionAutoPumps});
        Tests.push_back({"Like second admission while busy", &TestLikeSecondAdmissionWhileBusy});
        Tests.push_back({"Disabled Like flow rejects without leaks", &TestDisabledLikeFlowRejectsWithoutLeaks});
        Tests.push_back({"Like capacity rejection removes provisional payload", &TestLikeCapacityRejectionRemovesProvisionalPayload});
        Tests.push_back({"Like dispatch owns snapshot and is one shot", &TestLikeDispatchOwnsSnapshotAndIsOneShot});
        Tests.push_back({"Wrong-family Begin preserves ready Like", &TestWrongFamilyBeginPreservesReadyLike});
        Tests.push_back({"Like success completion confirms and cleans", &TestLikeSuccessCompletionConfirmsAndCleans});
        Tests.push_back({"Like cancel and failure clean terminal state", &TestLikeCancelAndFailureCleanTerminalState});
        Tests.push_back({"Wrong-family completion preserves Like InFlight", &TestWrongFamilyCompletionPreservesLikeInFlight});
        Tests.push_back({"Like completion captures ready Chat", &TestLikeCompletionCapturesReadyChat});
        Tests.push_back({"Share completion captures ready Like", &TestShareCompletionCapturesReadyLike});
        Tests.push_back({"Pending Like expires while Chat is Processing", &TestPendingLikeExpiresWhileChatIsProcessing});
    }
}
