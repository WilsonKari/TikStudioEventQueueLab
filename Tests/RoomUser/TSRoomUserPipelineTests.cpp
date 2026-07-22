#include "EventPipeline/Families/TSRoomUserFamily.h"
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

    void TestRoomUserCandidateAndAdmissionDefaults()
    {
        const FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        const TTSFamilyDecision<FTSRoomUserPayload> Decision =
            FTSRoomUserFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "RoomUser must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSRoomUserPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::RoomUser,
            "RoomUser candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::RoomUser,
            "RoomUser candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow !=
                    ETSEventFlow::RoomUserMilestone &&
                Candidate.EnqueueRequest.Flow !=
                    ETSEventFlow::RoomUserTop1Change,
            "RoomUser A must not select a derived flow"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "RoomUser must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "RoomUser candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "RoomUser candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "RoomUser candidate must not request eviction protection"
        );
        RequireRoomUserInputEqual(
            Candidate.Payload.Input,
            Input,
            "RoomUser candidate payload"
        );
    }

    void TestRoomUserPayloadSnapshotAndInputPreservation()
    {
        FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        const FTSRoomUserInput Original = Input;

        const TTSFamilyDecision<FTSRoomUserPayload> Decision =
            FTSRoomUserFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "RoomUser snapshot must produce a candidate"
        );
        RequireRoomUserInputEqual(
            Input,
            Original,
            "RoomUser input after by-copy decision"
        );

        Input.ViewerCount = 0;
        Input.TopGifterRank = 0;
        Input.TopViewers[0].UniqueId = "mutated-room-viewer";
        Input.TopViewers[0].Nickname.clear();
        Input.TopViewers[0].ProfilePictureUrl.clear();
        Input.TopViewers[0].CoinCount = 0;
        Input.TopViewers[0].bIsModerator = false;
        Input.TopViewers[0].bIsSubscriber = true;
        Input.TopViewers[0].GifterLevel = 0;
        Input.TopViewers[0].TeamMemberLevel = 0;
        std::swap(Input.TopViewers[0], Input.TopViewers[1]);
        Input.TopViewers.pop_back();

        RequireRoomUserInputEqual(
            Decision->Payload.Input,
            Original,
            "RoomUser payload snapshot"
        );
    }

    void TestRoomUserFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSRoomUserInput ExpectedInput = MakeCompleteRoomUserInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitRoomUser(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First RoomUser admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow == ETSEventFlow::RoomUser,
            "First RoomUser admission envelope mismatch"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First RoomUser admission must Auto Pump"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 1,
            "First RoomUser admission authority counts mismatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [EmissionId](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.EmissionId == EmissionId &&
                            Binding.FamilyKind ==
                                ETSEventFamilyKind::RoomUser &&
                            Binding.ExpectedFlow == ETSEventFlow::RoomUser &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "First RoomUser binding mismatch"
                    );
                }
            ),
            "First RoomUser binding must exist"
        );
        Require(
            Coordinator.VisitRoomUserPayloadForEmission(
                EmissionId,
                [&](const FTSRoomUserPayload& Payload)
                {
                    RequireRoomUserInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First RoomUser payload"
                    );
                }
            ),
            "First RoomUser payload must exist"
        );

        Require(
            BeginReadyRoomUser(Coordinator) == EmissionId,
            "First RoomUser must enter Processing"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "First RoomUser binding must enter Processing"
                    );
                }
            ),
            "First RoomUser Processing binding must exist"
        );
        const FTSRoomUserProcessingCompletionResult Completion =
            Coordinator.CompleteRoomUserProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0,
            "First RoomUser completion must clean authorities"
        );
    }

    void TestRoomUserSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeRoomUserSettings(true, 10)
        );
        FTSRoomUserInput FirstInput = MakeCompleteRoomUserInput();
        FirstInput.TopViewers[0].UniqueId = "first-room-viewer";
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitRoomUser(std::move(FirstInput));
        FTSRoomUserInput SecondInput = MakeCompleteRoomUserInput();
        SecondInput.TopViewers[0].UniqueId = "second-room-viewer";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitRoomUser(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both RoomUser admissions must be accepted"
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
                Coordinator.GetRoomUserPayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::RoomUser,
            "Busy RoomUser admissions must preserve both authorities"
        );

        Require(
            BeginReadyRoomUser(Coordinator) == FirstId,
            "Busy RoomUser admission must preserve the first ready"
        );
        const FTSRoomUserProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteRoomUserProcessing(
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
                Coordinator.GetRoomUserPayloadCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::RoomUser,
            "First RoomUser confirmation must expose the second ready"
        );

        Require(
            BeginReadyRoomUser(Coordinator) == SecondId,
            "Second RoomUser must become dispatchable"
        );
        const FTSRoomUserProcessingCompletionResult SecondCompletion =
            Coordinator.CompleteRoomUserProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        Require(
            SecondCompletion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Both RoomUser admissions must finish without authorities"
        );
    }

    void TestDisabledRoomUserFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeRoomUserSettings(false, 1)
        );
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitRoomUser(MakeCompleteRoomUserInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled RoomUser flow must be rejected"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled RoomUser rejection must not leak authorities"
        );
    }

    void TestRoomUserCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings = MakeRoomUserSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSRoomUserInput FirstInput = MakeCompleteRoomUserInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitRoomUser(FirstInput);
        FTSRoomUserInput RejectedInput = MakeCompleteRoomUserInput();
        RejectedInput.TopViewers[0].UniqueId = "rejected-room-viewer";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitRoomUser(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity RoomUser admission must succeed"
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second RoomUser admission must reject at capacity"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetRoomUserPayloadCount() == 1,
            "Capacity rejection must preserve only the prior RoomUser"
        );
        Require(
            Coordinator.VisitRoomUserPayloadForEmission(
                FirstId,
                [&](const FTSRoomUserPayload& Payload)
                {
                    RequireRoomUserInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity RoomUser"
                    );
                }
            ),
            "Prior RoomUser payload must remain available"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == FirstId,
            "Explicit Pump must select the preserved RoomUser"
        );
        Require(
            BeginReadyRoomUser(Coordinator) == FirstId,
            "Preserved RoomUser must enter Processing"
        );
        (void)Coordinator.CompleteRoomUserProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0,
            "Capacity scenario must finish without authorities"
        );
    }

    void TestRoomUserDispatchOwnsSnapshotAndIsOneShot()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSRoomUserInput Input = MakeCompleteRoomUserInput();
        const FTSRoomUserInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitRoomUser(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned RoomUser dispatch admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;

        Input.ViewerCount = 0;
        Input.TopGifterRank = 0;
        Input.TopViewers[0].UniqueId = "mutated-caller-room-viewer";
        Input.TopViewers[0].Nickname.clear();
        Input.TopViewers[0].ProfilePictureUrl.clear();
        Input.TopViewers[0].CoinCount = 0;
        Input.TopViewers[0].bIsModerator = false;
        Input.TopViewers[0].bIsSubscriber = true;
        Input.TopViewers[0].GifterLevel = 0;
        Input.TopViewers[0].TeamMemberLevel = 0;
        std::swap(Input.TopViewers[0], Input.TopViewers[1]);
        Input.TopViewers.pop_back();

        FTSRoomUserDispatchResult DispatchResult =
            Coordinator.BeginRoomUserProcessing();
        Require(
            DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched &&
                DispatchResult.Dispatch.has_value(),
            "Authorized RoomUser ready must dispatch"
        );
        Require(
            DispatchResult.Dispatch->Emission.EmissionId == EmissionId &&
                DispatchResult.Dispatch->Emission.Flow ==
                    ETSEventFlow::RoomUser,
            "RoomUser dispatch envelope mismatch"
        );
        RequireRoomUserInputEqual(
            DispatchResult.Dispatch->Payload.Input,
            ExpectedInput,
            "RoomUser dispatch payload"
        );

        FTSRoomUserInput& DispatchInput =
            DispatchResult.Dispatch->Payload.Input;
        DispatchInput.ViewerCount = 0;
        DispatchInput.TopGifterRank = 0;
        DispatchInput.TopViewers[0].UniqueId =
            "mutated-dispatch-room-viewer";
        DispatchInput.TopViewers[0].Nickname.clear();
        DispatchInput.TopViewers[0].ProfilePictureUrl.clear();
        DispatchInput.TopViewers[0].CoinCount = 0;
        DispatchInput.TopViewers[0].bIsModerator = false;
        DispatchInput.TopViewers[0].bIsSubscriber = true;
        DispatchInput.TopViewers[0].GifterLevel = 0;
        DispatchInput.TopViewers[0].TeamMemberLevel = 0;
        std::swap(
            DispatchInput.TopViewers[0],
            DispatchInput.TopViewers[1]
        );
        DispatchInput.TopViewers.pop_back();

        Require(
            Coordinator.VisitRoomUserPayloadForEmission(
                EmissionId,
                [&](const FTSRoomUserPayload& StoredPayload)
                {
                    RequireRoomUserInputEqual(
                        StoredPayload.Input,
                        ExpectedInput,
                        "Stored RoomUser after dispatch mutation"
                    );
                }
            ),
            "Stored RoomUser must remain after dispatch mutation"
        );
        const FTSRoomUserDispatchResult SecondDispatch =
            Coordinator.BeginRoomUserProcessing();
        Require(
            SecondDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value() &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "RoomUser dispatch must consume its ready exactly once"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "RoomUser dispatch must authorize Processing"
                    );
                }
            ) && Coordinator.GetRoomUserPayloadCount() == 1,
            "RoomUser authorities must remain until completion"
        );
        (void)Coordinator.CompleteRoomUserProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongFamilyBeginPreservesReadyRoomUser()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
            Coordinator,
            "wrong-begin-room-viewer"
        );

        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                        ETSEventFamilyKind::RoomUser &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetRoomUserPayloadCount() == 1 &&
                    Coordinator.VisitRoomUserPayloadForEmission(
                        RoomUserId,
                        [](const FTSRoomUserPayload&)
                        {
                        }
                    ),
                "Wrong-family Begin must preserve RoomUser authorities"
            );
            Require(
                Coordinator.VisitEmissionBinding(
                    RoomUserId,
                    [](const FTSEmissionBinding& Binding)
                    {
                        Require(
                            Binding.FamilyKind ==
                                    ETSEventFamilyKind::RoomUser &&
                                Binding.ExpectedFlow ==
                                    ETSEventFlow::RoomUser &&
                                Binding.ExternalState ==
                                    ETSExternalEmissionState::Bound,
                            "Wrong-family Begin must preserve RoomUser Bound"
                        );
                    }
                ),
                "Wrong-family Begin must preserve RoomUser binding"
            );
        };

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !ChatDispatch.Dispatch.has_value(),
            "Chat Begin must not consume RoomUser ready"
        );
        RequirePreserved();

        const FTSFollowDispatchResult FollowDispatch =
            Coordinator.BeginFollowProcessing();
        Require(
            FollowDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !FollowDispatch.Dispatch.has_value(),
            "Follow Begin must not consume RoomUser ready"
        );
        RequirePreserved();

        const FTSShareDispatchResult ShareDispatch =
            Coordinator.BeginShareProcessing();
        Require(
            ShareDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !ShareDispatch.Dispatch.has_value(),
            "Share Begin must not consume RoomUser ready"
        );
        RequirePreserved();

        const FTSLikeDispatchResult LikeDispatch =
            Coordinator.BeginLikeProcessing();
        Require(
            LikeDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !LikeDispatch.Dispatch.has_value(),
            "Like Begin must not consume RoomUser ready"
        );
        RequirePreserved();

        Require(
            BeginReadyRoomUser(Coordinator) == RoomUserId,
            "RoomUser Begin must dispatch the preserved ready"
        );
        (void)Coordinator.CompleteRoomUserProcessing(
            RoomUserId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestRoomUserSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
            Coordinator,
            "successful-room-viewer"
        );
        Require(
            BeginReadyRoomUser(Coordinator) == RoomUserId,
            "Successful RoomUser must enter Processing"
        );

        const FTSRoomUserProcessingCompletionResult Completion =
            Coordinator.CompleteRoomUserProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == RoomUserId &&
                Completion.ProcessingResult ==
                    ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Successful RoomUser completion result mismatch"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == RoomUserId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful RoomUser terminal mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                RoomUserId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitRoomUserPayloadForEmission(
                    RoomUserId,
                    [](const FTSRoomUserPayload&)
                    {
                    }
                ) &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Successful RoomUser completion must clean authorities"
        );
    }

    void TestRoomUserCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminalScenario = [](
            ETSProcessingResult ProcessingResult
        )
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-room-viewer"
                    : "failed-room-viewer"
            );
            Require(
                BeginReadyRoomUser(Coordinator) == RoomUserId,
                "Terminal RoomUser must enter Processing"
            );
            const FTSRoomUserProcessingCompletionResult Completion =
                Coordinator.CompleteRoomUserProcessing(
                    RoomUserId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == RoomUserId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed RoomUser must expose CancelResult"
            );
            Require(
                Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == RoomUserId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed RoomUser terminal mismatch"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetRoomUserPayloadCount() == 0 &&
                    !Coordinator.PeekPendingReadyFamilyKind().has_value(),
                "Terminal RoomUser must clean without retry"
            );
        };

        RunTerminalScenario(ETSProcessingResult::Cancelled);
        RunTerminalScenario(ETSProcessingResult::Failed);
    }

    void TestWrongFamilyCompletionPreservesRoomUserInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
            Coordinator,
            "wrong-completion-room-viewer"
        );
        Require(
            BeginReadyRoomUser(Coordinator) == RoomUserId,
            "Wrong-family completion RoomUser must enter Processing"
        );

        const auto RequireLogicError = [](auto&& Callback)
        {
            bool bThrew = false;
            try
            {
                Callback();
            }
            catch (const std::logic_error&)
            {
                bThrew = true;
            }
            Require(bThrew, "Wrong-family completion must throw");
        };

        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteChatProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteFollowProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteShareProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteLikeProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        });

        Require(
            Coordinator.VisitEmissionBinding(
                RoomUserId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind ==
                                ETSEventFamilyKind::RoomUser &&
                            Binding.ExpectedFlow == ETSEventFlow::RoomUser &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Processing,
                        "Wrong completion must preserve RoomUser Processing"
                    );
                }
            ) &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetRoomUserPayloadCount() == 1 &&
                Coordinator.VisitRoomUserPayloadForEmission(
                    RoomUserId,
                    [](const FTSRoomUserPayload&)
                    {
                    }
                ),
            "Wrong completion must preserve RoomUser authorities"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with RoomUser InFlight"
        );

        const FTSRoomUserProcessingCompletionResult Completion =
            Coordinator.CompleteRoomUserProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0,
            "Correct RoomUser completion must clean authorities"
        );
    }

    void TestRoomUserCompletionCapturesReadyChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
            Coordinator,
            "room-before-chat-viewer"
        );
        Require(
            BeginReadyRoomUser(Coordinator) == RoomUserId,
            "RoomUser must enter Processing before Chat"
        );

        FTSChatInput ExpectedChat = MakeCompleteInput();
        ExpectedChat.Comment = "Chat after RoomUser";
        const FTSPipelineAdmissionResult ChatAdmission =
            Coordinator.SubmitChat(ExpectedChat);
        Require(
            ChatAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ChatAdmission.EnqueueResult.has_value(),
            "Chat after RoomUser must be accepted"
        );
        const FTSEmissionId ChatId =
            ChatAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSRoomUserProcessingCompletionResult Completion =
            Coordinator.CompleteRoomUserProcessing(
                RoomUserId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == ChatId &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat,
            "RoomUser confirmation must expose pending Chat"
        );

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ChatDispatch.Dispatch.has_value() &&
                ChatDispatch.Dispatch->Emission.EmissionId == ChatId,
            "Chat selected after RoomUser must dispatch"
        );
        RequireChatPayloadMatchesInput(
            ChatDispatch.Dispatch->Payload,
            ExpectedChat,
            "Chat after RoomUser payload"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestLikeCompletionCapturesReadyRoomUser()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId LikeId =
            SubmitAcceptedLike(Coordinator, "like-before-room-user");
        Require(
            BeginReadyLike(Coordinator) == LikeId,
            "Like must enter Processing before RoomUser"
        );

        FTSRoomUserInput ExpectedRoomUser = MakeCompleteRoomUserInput();
        ExpectedRoomUser.TopViewers[0].UniqueId =
            "room-user-after-like";
        const FTSPipelineAdmissionResult RoomUserAdmission =
            Coordinator.SubmitRoomUser(ExpectedRoomUser);
        Require(
            RoomUserAdmission.Status ==
                    ETSPipelineAdmissionStatus::Accepted &&
                RoomUserAdmission.EnqueueResult.has_value(),
            "RoomUser after Like must be accepted"
        );
        const FTSEmissionId RoomUserId =
            RoomUserAdmission.EnqueueResult->AdmittedEmission.EmissionId;

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
                        .EmissionId == RoomUserId &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::RoomUser,
            "Like confirmation must expose pending RoomUser"
        );

        FTSRoomUserDispatchResult RoomUserDispatch =
            Coordinator.BeginRoomUserProcessing();
        Require(
            RoomUserDispatch.Status ==
                    ETSPipelineDispatchStatus::Dispatched &&
                RoomUserDispatch.Dispatch.has_value() &&
                RoomUserDispatch.Dispatch->Emission.EmissionId == RoomUserId &&
                RoomUserDispatch.Dispatch->Emission.Flow ==
                    ETSEventFlow::RoomUser,
            "RoomUser selected after Like must dispatch"
        );
        RequireRoomUserInputEqual(
            RoomUserDispatch.Dispatch->Payload.Input,
            ExpectedRoomUser,
            "RoomUser after Like payload"
        );
        (void)Coordinator.CompleteRoomUserProcessing(
            RoomUserId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestPendingRoomUserExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalRoomUserSettings(
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
            "Processing while RoomUser expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before RoomUser"
        );
        const FTSEmissionId RoomUserId = SubmitAcceptedRoomUser(
            Coordinator,
            "expiring-room-viewer"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    RoomUserId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::RoomUser &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending RoomUser expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    RoomUserId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Expired RoomUser must leave only Processing Chat"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "RoomUser expiration must preserve Chat Processing"
                    );
                }
            ),
            "Processing Chat binding must remain available"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Chat after RoomUser expiration"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterRoomUserPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "RoomUser candidate and admission defaults",
            &TestRoomUserCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "RoomUser payload snapshot and input preservation",
            &TestRoomUserPayloadSnapshotAndInputPreservation
        });
    }

    void RegisterRoomUserPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "RoomUser first admission Auto Pumps",
            &TestRoomUserFirstAdmissionAutoPumps
        });
        Tests.push_back({
            "RoomUser second admission while busy",
            &TestRoomUserSecondAdmissionWhileBusy
        });
        Tests.push_back({
            "Disabled RoomUser flow rejects without leaks",
            &TestDisabledRoomUserFlowRejectsWithoutLeaks
        });
        Tests.push_back({
            "RoomUser capacity rejection removes provisional payload",
            &TestRoomUserCapacityRejectionRemovesProvisionalPayload
        });
        Tests.push_back({
            "RoomUser dispatch owns snapshot and is one shot",
            &TestRoomUserDispatchOwnsSnapshotAndIsOneShot
        });
        Tests.push_back({
            "Wrong-family Begin preserves ready RoomUser",
            &TestWrongFamilyBeginPreservesReadyRoomUser
        });
        Tests.push_back({
            "RoomUser success completion confirms and cleans",
            &TestRoomUserSuccessCompletionConfirmsAndCleans
        });
        Tests.push_back({
            "RoomUser cancel and failure clean terminal state",
            &TestRoomUserCancelAndFailureCleanTerminalState
        });
        Tests.push_back({
            "Wrong-family completion preserves RoomUser InFlight",
            &TestWrongFamilyCompletionPreservesRoomUserInFlight
        });
        Tests.push_back({
            "RoomUser completion captures ready Chat",
            &TestRoomUserCompletionCapturesReadyChat
        });
        Tests.push_back({
            "Like completion captures ready RoomUser",
            &TestLikeCompletionCapturesReadyRoomUser
        });
        Tests.push_back({
            "Pending RoomUser expires while Chat is Processing",
            &TestPendingRoomUserExpiresWhileChatIsProcessing
        });
    }
}
