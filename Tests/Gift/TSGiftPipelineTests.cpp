#include "EventPipeline/Families/TSGiftFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using namespace TikStudio::Tests;
    using namespace std::chrono_literals;

    void TestGiftCandidateAndAdmissionDefaults()
    {
        const FTSGiftInput Input = MakeCompleteGiftInput();
        const TTSFamilyDecision<FTSGiftPayload> Decision =
            FTSGiftFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Gift must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSGiftPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Gift,
            "Gift candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Gift,
            "Gift candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow != ETSEventFlow::GiftCombo,
            "Gift repeat metadata must not select GiftCombo"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Gift metadata must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Gift candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride ==
                std::chrono::milliseconds{0},
            "Gift candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Gift candidate must not request eviction protection"
        );
        RequireGiftInputEqual(
            Candidate.Payload.Input,
            Input,
            "Gift candidate payload"
        );
    }

    void TestGiftPayloadSnapshotAndInputPreservation()
    {
        FTSGiftInput Input = MakeCompleteGiftInput();
        const FTSGiftInput Original = Input;

        const TTSFamilyDecision<FTSGiftPayload> Decision =
            FTSGiftFamily::Decide(Input);

        Require(Decision.has_value(), "Gift snapshot must produce a candidate");
        RequireGiftInputEqual(
            Input,
            Original,
            "Gift input after by-copy decision"
        );

        Input.GiftId = 0;
        Input.GiftName = "Mutated Gift";
        Input.GiftPictureUrl.clear();
        Input.DiamondCount = 0;
        Input.RepeatCount = 0;
        Input.GiftType = 0;
        Input.Describe = "Mutated description";
        Input.bRepeatEnd = false;
        Input.GroupId.clear();
        Input.User.UniqueId = "mutated-gift-user";
        Input.User.Nickname = "Mutated Gift User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireGiftInputEqual(
            Decision->Payload.Input,
            Original,
            "Gift payload snapshot"
        );
    }

    void TestGiftFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSGiftInput ExpectedInput = MakeCompleteGiftInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGift(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First Gift admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow == ETSEventFlow::Gift &&
                CoreResult.AdmittedEmission.Flow != ETSEventFlow::GiftCombo,
            "First Gift admission envelope mismatch"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First Gift admission must Auto Pump"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 1,
            "First Gift admission authority counts mismatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [EmissionId](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.EmissionId == EmissionId &&
                            Binding.FamilyKind == ETSEventFamilyKind::Gift &&
                            Binding.ExpectedFlow == ETSEventFlow::Gift &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "First Gift binding mismatch"
                    );
                }
            ),
            "First Gift binding must exist"
        );
        Require(
            Coordinator.VisitGiftPayloadForEmission(
                EmissionId,
                [&](const FTSGiftPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First Gift payload"
                    );
                }
            ),
            "First Gift payload must exist"
        );

        Require(
            BeginReadyGift(Coordinator) == EmissionId,
            "First Gift must enter Processing"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "First Gift binding must enter Processing"
                    );
                }
            ),
            "First Gift Processing binding must exist"
        );
        const FTSGiftProcessingCompletionResult Completion =
            Coordinator.CompleteGiftProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "First Gift completion must clean authorities"
        );
    }

    void TestGiftSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(MakeGiftSettings(true, 10));
        FTSGiftInput FirstInput = MakeCompleteGiftInput();
        FirstInput.User.UniqueId = "first-gift-user";
        FirstInput.GroupId = "first-gift-group";
        FirstInput.RepeatCount = 3;
        const FTSGiftInput ExpectedFirst = FirstInput;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitGift(std::move(FirstInput));

        FTSGiftInput SecondInput = MakeCompleteGiftInput();
        SecondInput.User.UniqueId = "second-gift-user";
        SecondInput.GroupId = "second-gift-group";
        SecondInput.RepeatCount = 11;
        const FTSGiftInput ExpectedSecond = SecondInput;
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitGift(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both Gift admissions must be accepted"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSEmissionId SecondId =
            Second.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            FirstId != SecondId &&
                First.EnqueueResult->AdmittedEmission.Flow ==
                    ETSEventFlow::Gift &&
                Second.EnqueueResult->AdmittedEmission.Flow ==
                    ETSEventFlow::Gift &&
                First.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Second.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetGiftPayloadCount() == 2 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift,
            "Busy Gift admissions must preserve both authorities"
        );
        Require(
            Coordinator.VisitGiftPayloadForEmission(
                FirstId,
                [&](const FTSGiftPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        ExpectedFirst,
                        "First busy Gift payload"
                    );
                }
            ) &&
                Coordinator.VisitGiftPayloadForEmission(
                    SecondId,
                    [&](const FTSGiftPayload& Payload)
                    {
                        RequireGiftInputEqual(
                            Payload.Input,
                            ExpectedSecond,
                            "Second busy Gift payload"
                        );
                    }
                ),
            "Busy Gift payload snapshots must remain distinct"
        );

        Require(
            BeginReadyGift(Coordinator) == FirstId,
            "Busy Gift admission must preserve the first ready"
        );
        const FTSGiftProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteGiftProcessing(
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
                Coordinator.GetGiftPayloadCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift,
            "First Gift confirmation must expose the second ready"
        );

        FTSGiftDispatchResult SecondDispatch =
            Coordinator.BeginGiftProcessing();
        Require(
            SecondDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                SecondDispatch.Dispatch.has_value() &&
                SecondDispatch.Dispatch->Emission.EmissionId == SecondId &&
                SecondDispatch.Dispatch->Emission.Flow == ETSEventFlow::Gift &&
                SecondDispatch.Dispatch->Emission.Flow !=
                    ETSEventFlow::GiftCombo,
            "Second Gift must become dispatchable"
        );
        RequireGiftInputEqual(
            SecondDispatch.Dispatch->Payload.Input,
            ExpectedSecond,
            "Second busy Gift dispatch"
        );
        const FTSGiftProcessingCompletionResult SecondCompletion =
            Coordinator.CompleteGiftProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        Require(
            SecondCompletion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Both Gift admissions must finish without authorities"
        );
    }

    void TestDisabledGiftFlowRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(MakeGiftSettings(false, 1));
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGift(MakeCompleteGiftInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled Gift flow must be rejected"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled Gift rejection must not leak authorities"
        );
    }

    void TestGiftCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings = MakeGiftSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSGiftInput FirstInput = MakeCompleteGiftInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitGift(FirstInput);
        FTSGiftInput RejectedInput = MakeCompleteGiftInput();
        RejectedInput.User.UniqueId = "rejected-gift-user";
        RejectedInput.GroupId = "rejected-gift-group";
        RejectedInput.RepeatCount = 99;
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitGift(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity Gift admission must succeed"
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second Gift admission must reject at capacity"
        );
        const FTSEmissionId FirstId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 1,
            "Capacity rejection must preserve only the prior Gift"
        );
        Require(
            Coordinator.VisitGiftPayloadForEmission(
                FirstId,
                [&](const FTSGiftPayload& Payload)
                {
                    RequireGiftInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity Gift"
                    );
                }
            ),
            "Prior Gift payload must remain available"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == FirstId,
            "Explicit Pump must select the preserved Gift"
        );
        Require(
            BeginReadyGift(Coordinator) == FirstId,
            "Preserved Gift must enter Processing"
        );
        (void)Coordinator.CompleteGiftProcessing(
            FirstId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            "Capacity scenario must finish without authorities"
        );
    }

    void TestGiftDispatchOwnsSnapshotAndIsOneShot()
    {
        const auto MutateInput = [](FTSGiftInput& Input)
        {
            Input.GiftId = 0;
            Input.GiftName = "Mutated Gift";
            Input.GiftPictureUrl.clear();
            Input.DiamondCount = 0;
            Input.RepeatCount = 0;
            Input.GiftType = 0;
            Input.Describe.clear();
            Input.bRepeatEnd = false;
            Input.GroupId.clear();
            Input.User.UniqueId = "mutated-gift-user";
            Input.User.Nickname.clear();
            Input.User.ProfilePictureUrl.clear();
            Input.User.FollowRole = 0;
            Input.User.bIsModerator = false;
            Input.User.bIsSubscriber = true;
            Input.User.bIsNewGifter = false;
            Input.User.TopGifterRank = 0;
            Input.User.GifterLevel = 0;
            Input.User.TeamMemberLevel = 0;
        };

        FTSEventPipelineCoordinator Coordinator;
        FTSGiftInput Input = MakeCompleteGiftInput();
        const FTSGiftInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitGift(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned Gift dispatch admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        MutateInput(Input);

        FTSGiftDispatchResult DispatchResult =
            Coordinator.BeginGiftProcessing();
        Require(
            DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched &&
                DispatchResult.Dispatch.has_value() &&
                DispatchResult.Dispatch->Emission.EmissionId == EmissionId &&
                DispatchResult.Dispatch->Emission.Flow == ETSEventFlow::Gift &&
                DispatchResult.Dispatch->Emission.Flow !=
                    ETSEventFlow::GiftCombo,
            "Authorized Gift ready must dispatch directly"
        );
        RequireGiftInputEqual(
            DispatchResult.Dispatch->Payload.Input,
            ExpectedInput,
            "Gift dispatch payload"
        );
        MutateInput(DispatchResult.Dispatch->Payload.Input);

        Require(
            Coordinator.VisitGiftPayloadForEmission(
                EmissionId,
                [&](const FTSGiftPayload& StoredPayload)
                {
                    RequireGiftInputEqual(
                        StoredPayload.Input,
                        ExpectedInput,
                        "Stored Gift after dispatch mutation"
                    );
                }
            ),
            "Stored Gift must remain after dispatch mutation"
        );
        const FTSGiftDispatchResult SecondDispatch =
            Coordinator.BeginGiftProcessing();
        Require(
            SecondDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value() &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Gift dispatch must consume its ready exactly once"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Gift dispatch must authorize Processing"
                    );
                }
            ) && Coordinator.GetGiftPayloadCount() == 1,
            "Gift authorities must remain until completion"
        );
        (void)Coordinator.CompleteGiftProcessing(
            EmissionId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestWrongFamilyBeginPreservesReadyGift()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "wrong-begin-gift-user");

        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                        ETSEventFamilyKind::Gift &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetGiftPayloadCount() == 1 &&
                    Coordinator.VisitGiftPayloadForEmission(
                        GiftId,
                        [](const FTSGiftPayload&)
                        {
                        }
                    ),
                "Wrong-family Begin must preserve Gift authorities"
            );
            Require(
                Coordinator.VisitEmissionBinding(
                    GiftId,
                    [](const FTSEmissionBinding& Binding)
                    {
                        Require(
                            Binding.FamilyKind == ETSEventFamilyKind::Gift &&
                                Binding.ExpectedFlow == ETSEventFlow::Gift &&
                                Binding.ExternalState ==
                                    ETSExternalEmissionState::Bound,
                            "Wrong-family Begin must preserve Gift binding"
                        );
                    }
                ),
                "Ready Gift binding must remain available"
            );
        };
        const auto RequireNoDispatch = [](const auto& Result)
        {
            Require(
                Result.Status == ETSPipelineDispatchStatus::NoEmissionReady &&
                    !Result.Dispatch.has_value(),
                "Wrong-family Begin must not consume ready Gift"
            );
        };

        RequireNoDispatch(Coordinator.BeginChatProcessing());
        RequirePreserved();
        RequireNoDispatch(Coordinator.BeginFollowProcessing());
        RequirePreserved();
        RequireNoDispatch(Coordinator.BeginShareProcessing());
        RequirePreserved();
        RequireNoDispatch(Coordinator.BeginLikeProcessing());
        RequirePreserved();
        RequireNoDispatch(Coordinator.BeginRoomUserProcessing());
        RequirePreserved();

        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Correct Gift Begin must consume the preserved ready"
        );
        (void)Coordinator.CompleteGiftProcessing(
            GiftId,
            ETSProcessingResult::Succeeded
        );
    }

    void TestGiftSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "successful-gift-user");
        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Successful Gift must enter Processing"
        );

        const FTSGiftProcessingCompletionResult Completion =
            Coordinator.CompleteGiftProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.EmissionId == GiftId &&
                Completion.ProcessingResult ==
                    ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Successful Gift must expose only ConfirmResult"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == GiftId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful Gift confirmation lifecycle mismatch"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                GiftId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitGiftPayloadForEmission(
                    GiftId,
                    [](const FTSGiftPayload&)
                    {
                    }
                ) &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Successful Gift completion must clean authorities"
        );
    }

    void TestGiftCancelAndFailureCleanTerminalState()
    {
        const auto RunTerminalScenario = [](
            ETSProcessingResult ProcessingResult
        )
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId GiftId = SubmitAcceptedGift(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-gift-user"
                    : "failed-gift-user"
            );
            Require(
                BeginReadyGift(Coordinator) == GiftId,
                "Terminal Gift must enter Processing"
            );
            const FTSGiftProcessingCompletionResult Completion =
                Coordinator.CompleteGiftProcessing(
                    GiftId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == GiftId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed Gift must expose CancelResult"
            );
            Require(
                Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == GiftId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed Gift terminal mismatch"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetGiftPayloadCount() == 0 &&
                    !Coordinator.PeekPendingReadyFamilyKind().has_value(),
                "Terminal Gift must clean without retry"
            );
        };

        RunTerminalScenario(ETSProcessingResult::Cancelled);
        RunTerminalScenario(ETSProcessingResult::Failed);
    }

    void TestWrongFamilyCompletionPreservesGiftInFlight()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "wrong-completion-gift-user");
        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Wrong-family completion Gift must enter Processing"
        );

        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.VisitEmissionBinding(
                    GiftId,
                    [](const FTSEmissionBinding& Binding)
                    {
                        Require(
                            Binding.FamilyKind == ETSEventFamilyKind::Gift &&
                                Binding.ExpectedFlow == ETSEventFlow::Gift &&
                                Binding.ExternalState ==
                                    ETSExternalEmissionState::Processing,
                            "Wrong completion must preserve Gift Processing"
                        );
                    }
                ) &&
                    Coordinator.GetBindingCount() == 1 &&
                    Coordinator.GetGiftPayloadCount() == 1 &&
                    Coordinator.VisitGiftPayloadForEmission(
                        GiftId,
                        [](const FTSGiftPayload&)
                        {
                        }
                    ),
                "Wrong completion must preserve Gift authorities"
            );
        };
        const auto RequireLogicError = [&](auto&& Callback)
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
            RequirePreserved();
        };

        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteChatProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteFollowProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteShareProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteLikeProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireLogicError([&]()
        {
            (void)Coordinator.CompleteRoomUserProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        });
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Gift InFlight"
        );

        const FTSGiftProcessingCompletionResult Completion =
            Coordinator.CompleteGiftProcessing(
                GiftId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Correct Gift completion must clean authorities"
        );
    }

    void TestGiftCompletionCapturesReadyChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "gift-before-chat-user");
        Require(
            BeginReadyGift(Coordinator) == GiftId,
            "Gift must enter Processing before Chat"
        );

        FTSChatInput ExpectedChat = MakeCompleteInput();
        ExpectedChat.Comment = "Chat after Gift";
        const FTSPipelineAdmissionResult ChatAdmission =
            Coordinator.SubmitChat(ExpectedChat);
        Require(
            ChatAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ChatAdmission.EnqueueResult.has_value() &&
                ChatAdmission.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Chat after Gift must be accepted"
        );
        const FTSEmissionId ChatId =
            ChatAdmission.EnqueueResult->AdmittedEmission.EmissionId;

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
                        .EmissionId == ChatId &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat,
            "Gift confirmation must expose pending Chat"
        );

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ChatDispatch.Dispatch.has_value() &&
                ChatDispatch.Dispatch->Emission.EmissionId == ChatId,
            "Chat selected after Gift must dispatch"
        );
        RequireChatPayloadMatchesInput(
            ChatDispatch.Dispatch->Payload,
            ExpectedChat,
            "Chat after Gift payload"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            "Gift then Chat scenario must clean authorities"
        );
    }

    void TestChatCompletionCapturesReadyGift()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ChatId =
            SubmitAcceptedChat(Coordinator, "Chat before Gift");
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Gift"
        );

        FTSGiftInput ExpectedGift = MakeCompleteGiftInput();
        ExpectedGift.User.UniqueId = "gift-after-chat-user";
        ExpectedGift.GroupId = "gift-after-chat-group";
        ExpectedGift.RepeatCount = 23;
        ExpectedGift.GiftType = 7;
        ExpectedGift.bRepeatEnd = true;
        const FTSPipelineAdmissionResult GiftAdmission =
            Coordinator.SubmitGift(ExpectedGift);
        Require(
            GiftAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                GiftAdmission.EnqueueResult.has_value() &&
                GiftAdmission.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Gift after Chat must be accepted"
        );
        const FTSEmissionId GiftId =
            GiftAdmission.EnqueueResult->AdmittedEmission.EmissionId;

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                ChatId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == GiftId &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1 &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Gift,
            "Chat confirmation must expose pending Gift"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                GiftId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Gift &&
                            Binding.ExpectedFlow == ETSEventFlow::Gift &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "Gift after Chat binding mismatch"
                    );
                }
            ) &&
                Coordinator.VisitGiftPayloadForEmission(
                    GiftId,
                    [&](const FTSGiftPayload& Payload)
                    {
                        RequireGiftInputEqual(
                            Payload.Input,
                            ExpectedGift,
                            "Gift after Chat stored payload"
                        );
                    }
                ),
            "Gift after Chat authorities must be available"
        );

        FTSGiftDispatchResult GiftDispatch =
            Coordinator.BeginGiftProcessing();
        Require(
            GiftDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                GiftDispatch.Dispatch.has_value() &&
                GiftDispatch.Dispatch->Emission.EmissionId == GiftId &&
                GiftDispatch.Dispatch->Emission.Flow == ETSEventFlow::Gift &&
                GiftDispatch.Dispatch->Emission.Flow !=
                    ETSEventFlow::GiftCombo,
            "Gift selected after Chat must dispatch directly"
        );
        RequireGiftInputEqual(
            GiftDispatch.Dispatch->Payload.Input,
            ExpectedGift,
            "Gift after Chat dispatch payload"
        );
        (void)Coordinator.CompleteGiftProcessing(
            GiftId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Chat then Gift scenario must clean authorities"
        );
    }

    void TestPendingGiftExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalGiftSettings(
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
            "Processing while Gift expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Gift"
        );
        const FTSEmissionId GiftId =
            SubmitAcceptedGift(Coordinator, "expiring-gift-user");
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Gift must remain Pending while Chat is Processing"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    GiftId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::Gift &&
                Expirations.LifecycleEvents.front().Envelope.Flow !=
                    ETSEventFlow::GiftCombo &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Gift expiration lifecycle mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    GiftId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ) &&
                !Coordinator.VisitGiftPayloadForEmission(
                    GiftId,
                    [](const FTSGiftPayload&)
                    {
                    }
                ) &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Expired Gift must leave only Processing Chat"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Gift expiration must preserve Chat Processing"
                    );
                }
            ),
            "Processing Chat binding must remain available"
        );
        Require(
            Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Chat after Gift expiration"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            "Gift expiration scenario must clean remaining Chat"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterGiftPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Gift candidate and admission defaults",
            &TestGiftCandidateAndAdmissionDefaults
        });
        Tests.push_back({
            "Gift payload snapshot and input preservation",
            &TestGiftPayloadSnapshotAndInputPreservation
        });
    }

    void RegisterGiftPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Gift first admission Auto Pumps",
            &TestGiftFirstAdmissionAutoPumps
        });
        Tests.push_back({
            "Gift second admission while busy",
            &TestGiftSecondAdmissionWhileBusy
        });
        Tests.push_back({
            "Disabled Gift flow rejects without leaks",
            &TestDisabledGiftFlowRejectsWithoutLeaks
        });
        Tests.push_back({
            "Gift capacity rejection removes provisional payload",
            &TestGiftCapacityRejectionRemovesProvisionalPayload
        });
        Tests.push_back({
            "Gift dispatch owns snapshot and is one shot",
            &TestGiftDispatchOwnsSnapshotAndIsOneShot
        });
        Tests.push_back({
            "Wrong-family Begin preserves ready Gift",
            &TestWrongFamilyBeginPreservesReadyGift
        });
        Tests.push_back({
            "Gift success completion confirms and cleans",
            &TestGiftSuccessCompletionConfirmsAndCleans
        });
        Tests.push_back({
            "Gift cancel and failure clean terminal state",
            &TestGiftCancelAndFailureCleanTerminalState
        });
        Tests.push_back({
            "Wrong-family completion preserves Gift InFlight",
            &TestWrongFamilyCompletionPreservesGiftInFlight
        });
        Tests.push_back({
            "Gift completion captures ready Chat",
            &TestGiftCompletionCapturesReadyChat
        });
        Tests.push_back({
            "Chat completion captures ready Gift",
            &TestChatCompletionCapturesReadyGift
        });
        Tests.push_back({
            "Pending Gift expires while Chat is Processing",
            &TestPendingGiftExpiresWhileChatIsProcessing
        });
    }
}
