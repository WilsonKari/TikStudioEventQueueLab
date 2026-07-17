#include "EventPipeline/Families/TSFollowFamily.h"
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
    using namespace TikStudio::Tests;

    void TestFollowCandidateAndAdmissionDefaults()
    {
        FTSFollowInput Input;
        Input.User.UniqueId = "follow-user";

        const TTSFamilyDecision<FTSFollowPayload> Decision =
            FTSFollowFamily::Decide(Input);

        Require(
            Decision.has_value(),
            "Follow must produce an admission candidate"
        );

        const TTSAdmissionCandidate<FTSFollowPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Follow,
            "Follow candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Follow,
            "Follow candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Follow candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Follow candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride.count() == 0,
            "Follow candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Follow candidate must not request special eviction protection"
        );
    }

    void TestFollowPayloadSnapshotAndInputPreservation()
    {
        FTSFollowInput Input = MakeCompleteFollowInput();
        const FTSFollowInput Original = Input;

        const TTSFamilyDecision<FTSFollowPayload> Decision =
            FTSFollowFamily::Decide(Input);

        Require(Decision.has_value(), "Follow snapshot must produce a candidate");
        RequireUserEqual(
            Input.User,
            Original.User,
            "Follow input after by-copy decision"
        );

        Input.User.UniqueId = "mutated-follow-user";
        Input.User.Nickname = "Mutated Follow User";
        Input.User.ProfilePictureUrl.clear();
        Input.User.FollowRole = 0;
        Input.User.bIsModerator = false;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = false;
        Input.User.TopGifterRank = 0;
        Input.User.GifterLevel = 0;
        Input.User.TeamMemberLevel = 0;

        RequireUserEqual(
            Decision->Payload.Input.User,
            Original.User,
            "Follow payload snapshot"
        );
    }

    void TestFollowCoordinatorFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSFollowInput ExpectedInput = MakeCompleteFollowInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitFollow(ExpectedInput);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "First Follow admission must be accepted"
        );
        const FTSEnqueueResult& CoreResult = *Admission.EnqueueResult;
        const FTSEmissionId EmissionId =
            CoreResult.AdmittedEmission.EmissionId;
        Require(
            EmissionId != 0 &&
                CoreResult.AdmittedEmission.Flow == ETSEventFlow::Follow,
            "First Follow admission envelope mismatch"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status ==
                ETSPumpStatus::EmissionReady &&
                CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId ==
                    EmissionId,
            "First Follow admission must Auto Pump"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 1,
            "First Follow admission authority counts mismatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Follow &&
                            Binding.ExpectedFlow == ETSEventFlow::Follow &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Bound,
                        "First Follow binding mismatch"
                    );
                }
            ),
            "First Follow binding must exist"
        );
        Require(
            Coordinator.VisitFollowPayloadForEmission(
                EmissionId,
                [&](const FTSFollowPayload& Payload)
                {
                    RequireFollowInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        "First Follow payload"
                    );
                }
            ),
            "First Follow payload must exist"
        );
    }

    void TestFollowCoordinatorSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitFollow(MakeCompleteFollowInput());
        FTSFollowInput SecondInput = MakeCompleteFollowInput();
        SecondInput.User.UniqueId = "second-follow-user";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitFollow(std::move(SecondInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value() &&
                Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Both Follow admissions must be accepted"
        );
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status ==
                ETSPumpStatus::EmissionReady &&
                Second.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested,
            "Busy Follow admission must not replace the first ready"
        );
        Require(
            First.EnqueueResult->AdmittedEmission.EmissionId !=
                Second.EnqueueResult->AdmittedEmission.EmissionId &&
                Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetFollowPayloadCount() == 2,
            "Busy Follow admissions must keep distinct authorities"
        );
        Require(
            BeginReadyFollow(Coordinator) ==
                First.EnqueueResult->AdmittedEmission.EmissionId,
            "Busy Follow admission must preserve the first ready"
        );
    }

    void TestFollowCoordinatorRejectsDisabledFlow()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeFollowSettings(false, 10)
        );
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitFollow(MakeCompleteFollowInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled Follow flow must be rejected by the core"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled Follow rejection must remove its provisional payload"
        );
    }

    void TestFollowCapacityRejectionPreservesPriorAdmission()
    {
        FTSEventQueueSettings Settings = MakeFollowSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSFollowInput FirstInput = MakeCompleteFollowInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitFollow(FirstInput);
        FTSFollowInput RejectedInput = MakeCompleteFollowInput();
        RejectedInput.User.UniqueId = "rejected-follow-user";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitFollow(std::move(RejectedInput));

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity Follow admission must succeed"
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second Follow admission must reject at capacity"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetFollowPayloadCount() == 1,
            "Capacity rejection must preserve only the prior Follow"
        );
        Require(
            Coordinator.VisitFollowPayloadForEmission(
                First.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSFollowPayload& Payload)
                {
                    RequireFollowInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Preserved capacity Follow"
                    );
                }
            ),
            "Prior Follow payload must remain available"
        );
    }

    void TestPendingReadyFamilyInspectionIsNonConsuming()
    {
        FTSEventPipelineCoordinator Coordinator;
        Require(
            !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Empty coordinator must not expose a ready family"
        );

        const FTSEmissionId EmissionId =
            SubmitAcceptedFollow(Coordinator, "peek-follow-user");
        const std::optional<ETSEventFamilyKind> FirstPeek =
            Coordinator.PeekPendingReadyFamilyKind();
        const std::optional<ETSEventFamilyKind> SecondPeek =
            Coordinator.PeekPendingReadyFamilyKind();

        Require(
            FirstPeek == ETSEventFamilyKind::Follow &&
                SecondPeek == ETSEventFamilyKind::Follow,
            "Peek must repeatedly report the pending Follow family"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Peek must not authorize processing"
                    );
                }
            ),
            "Peeked Follow binding must remain available"
        );
        Require(
            BeginReadyFollow(Coordinator) == EmissionId,
            "Peek must not consume the Follow ready"
        );
    }

    void TestWrongFamilyBeginPreservesPendingReady()
    {
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId FollowId =
                SubmitAcceptedFollow(Coordinator, "wrong-chat-route");

            Require(
                Coordinator.BeginChatProcessing().Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady,
                "Chat Begin must not dispatch a Follow ready"
            );
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Follow &&
                    BeginReadyFollow(Coordinator) == FollowId,
                "Wrong Chat Begin must preserve the Follow ready"
            );
        }

        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId ChatId =
                SubmitAcceptedChat(Coordinator, "Wrong Follow route");

            Require(
                Coordinator.BeginFollowProcessing().Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady,
                "Follow Begin must not dispatch a Chat ready"
            );
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat &&
                    BeginReadyChat(Coordinator) == ChatId,
                "Wrong Follow Begin must preserve the Chat ready"
            );
        }
    }

    void TestAuthorizedFollowReadyProducesOwnedOneShotDispatch()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSFollowInput ExpectedInput = MakeCompleteFollowInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitFollow(ExpectedInput);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Owned Follow dispatch admission failed"
        );
        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;

        FTSFollowDispatchResult DispatchResult =
            Coordinator.BeginFollowProcessing();
        Require(
            DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched &&
                DispatchResult.Dispatch.has_value(),
            "Authorized Follow ready must dispatch"
        );
        Require(
            DispatchResult.Dispatch->Emission.EmissionId == EmissionId &&
                DispatchResult.Dispatch->Emission.Flow == ETSEventFlow::Follow,
            "Follow dispatch envelope mismatch"
        );
        RequireFollowInputEqual(
            DispatchResult.Dispatch->Payload.Input,
            ExpectedInput,
            "Follow dispatch payload"
        );

        DispatchResult.Dispatch->Payload.Input.User.Nickname =
            "Mutated dispatch copy";
        Require(
            Coordinator.VisitFollowPayloadForEmission(
                EmissionId,
                [&](const FTSFollowPayload& StoredPayload)
                {
                    RequireFollowInputEqual(
                        StoredPayload.Input,
                        ExpectedInput,
                        "Stored Follow after dispatch mutation"
                    );
                }
            ),
            "Stored Follow must remain after dispatch"
        );
        Require(
            Coordinator.BeginFollowProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Follow dispatch must consume its ready exactly once"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Follow dispatch must authorize Processing"
                    );
                }
            ),
            "Dispatched Follow binding must remain available"
        );
    }

    void TestSuccessfulFollowCompletionCleansTerminalState()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedFollow(Coordinator, "successful-follow");
        Require(
            BeginReadyFollow(Coordinator) == EmissionId,
            "Successful Follow must enter Processing"
        );

        const FTSFollowProcessingCompletionResult Completion =
            Coordinator.CompleteFollowProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult == ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Successful Follow completion result mismatch"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Successful Follow completion terminal mismatch"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                !Coordinator.VisitEmissionBinding(
                    EmissionId,
                    [](const FTSEmissionBinding&)
                    {
                    }
                ),
            "Successful Follow completion must clean binding and payload"
        );
    }

    void TestSuccessfulFollowCompletionCapturesChatReady()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FollowId =
            SubmitAcceptedFollow(Coordinator, "follow-before-chat");
        const FTSEmissionId ChatId =
            SubmitAcceptedChat(Coordinator, "Chat after Follow");
        Require(
            BeginReadyFollow(Coordinator) == FollowId,
            "Follow must enter Processing before its completion"
        );

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
                        .EmissionId == ChatId,
            "Follow Confirm must expose the next Chat ready"
        );
        Require(
            Coordinator.PeekPendingReadyFamilyKind() ==
                ETSEventFamilyKind::Chat &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetBindingCount() == 1,
            "Follow completion must retain only the ready Chat authorities"
        );
        Require(
            Coordinator.BeginFollowProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady &&
                BeginReadyChat(Coordinator) == ChatId,
            "Follow Begin must preserve the Chat selected by confirmation"
        );
    }

    void TestCancelledAndFailedFollowCompletionUseCancellationTerminal()
    {
        const std::vector<ETSProcessingResult> ProcessingResults{
            ETSProcessingResult::Cancelled,
            ETSProcessingResult::Failed
        };

        for (const ETSProcessingResult ProcessingResult : ProcessingResults)
        {
            FTSEventPipelineCoordinator Coordinator;
            const FTSEmissionId EmissionId = SubmitAcceptedFollow(
                Coordinator,
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-follow"
                    : "failed-follow"
            );
            Require(
                BeginReadyFollow(Coordinator) == EmissionId,
                "Terminal Follow must enter Processing"
            );

            const FTSFollowProcessingCompletionResult Completion =
                Coordinator.CompleteFollowProcessing(
                    EmissionId,
                    ProcessingResult
                );
            Require(
                Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed Follow must expose only CancelResult"
            );
            Require(
                Completion.CancelResult->Status ==
                    ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == EmissionId &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed Follow must use the cancellation terminal"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetFollowPayloadCount() == 0,
                "Cancellation terminal must clean Follow authorities"
            );
        }
    }

    void TestFollowExpirationCleansDiscardAndConsolidatePayloads()
    {
        struct FExpirationCase
        {
            ETSEventExpirePolicy Policy = ETSEventExpirePolicy::Discard;
            ETSEmissionTerminalReason Reason =
                ETSEmissionTerminalReason::ExpiredDiscard;
        };

        const std::vector<FExpirationCase> Cases{
            {
                ETSEventExpirePolicy::Discard,
                ETSEmissionTerminalReason::ExpiredDiscard
            },
            {
                ETSEventExpirePolicy::Consolidate,
                ETSEmissionTerminalReason::ExpiredConsolidate
            }
        };

        for (const FExpirationCase& Case : Cases)
        {
            FControlledClock Clock;
            FTSEventPipelineCoordinator Coordinator(
                MakeOperationalFollowSettings(
                    false,
                    true,
                    5s,
                    Case.Policy
                ),
                Clock.MakeProvider()
            );
            const FTSEmissionId EmissionId =
                SubmitAcceptedFollow(Coordinator, "expiring-follow");

            Clock.Advance(5s);
            const FTSProcessDueExpirationsResult Expirations =
                Coordinator.ProcessDueExpirations();
            Require(
                Expirations.LifecycleEvents.size() == 1 &&
                    Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                        EmissionId &&
                    Expirations.LifecycleEvents.front().Envelope.Flow ==
                        ETSEventFlow::Follow &&
                    Expirations.LifecycleEvents.front().Reason == Case.Reason,
                "Follow expiration lifecycle mismatch"
            );
            Require(
                Coordinator.GetBindingCount() == 0 &&
                    Coordinator.GetFollowPayloadCount() == 0 &&
                    !Coordinator.PeekPendingReadyFamilyKind().has_value(),
                "Follow expiration must clean its binding and payload"
            );
        }
    }

    void TestMixedChatAndFollowLifecyclePreservesCoreOrder()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalChatSettings(
            true,
            true,
            5s,
            ETSEventExpirePolicy::Discard
        );
        FTSFlowQueueSettings* FollowSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Follow);
        Require(FollowSettings != nullptr, "Mixed Follow settings must exist");
        FollowSettings->bEnabled = true;
        FollowSettings->MaxSlots = 10;
        FollowSettings->TTL = 5s;
        FollowSettings->ExpirePolicy = ETSEventExpirePolicy::Discard;

        FTSEventPipelineCoordinator Coordinator(
            std::move(Settings),
            Clock.MakeProvider()
        );
        const FTSEmissionId ProcessingChatId =
            SubmitAcceptedChat(Coordinator, "Mixed Processing Chat");
        Require(
            BeginReadyChat(Coordinator) == ProcessingChatId,
            "Mixed Chat must enter Processing"
        );
        const FTSEmissionId ExpiringFollowId =
            SubmitAcceptedFollow(Coordinator, "mixed-expiring-follow");
        const FTSEmissionId ExpiringChatId =
            SubmitAcceptedChat(Coordinator, "Mixed expiring Chat");

        Clock.Advance(5s);
        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                ProcessingChatId,
                ETSProcessingResult::Succeeded
            );
        Require(
            Completion.ConfirmResult.has_value(),
            "Mixed lifecycle confirmation must succeed"
        );
        const FTSEmissionLifecycleEvents& LifecycleEvents =
            Completion.ConfirmResult->LifecycleEvents;
        Require(
            LifecycleEvents.size() == 3,
            "Mixed lifecycle must contain Confirmed and two expirations"
        );
        Require(
            LifecycleEvents[0].Envelope.EmissionId == ProcessingChatId &&
                LifecycleEvents[0].Envelope.Flow == ETSEventFlow::Chat &&
                LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Mixed lifecycle first event mismatch"
        );
        Require(
            LifecycleEvents[1].Envelope.EmissionId == ExpiringFollowId &&
                LifecycleEvents[1].Envelope.Flow == ETSEventFlow::Follow &&
                LifecycleEvents[1].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Mixed lifecycle second event must be Follow expiration"
        );
        Require(
            LifecycleEvents[2].Envelope.EmissionId == ExpiringChatId &&
                LifecycleEvents[2].Envelope.Flow == ETSEventFlow::Chat &&
                LifecycleEvents[2].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Mixed lifecycle third event must be Chat expiration"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Mixed lifecycle must clean each typed repository"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterFollowPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Follow candidate and admission defaults", &TestFollowCandidateAndAdmissionDefaults});
        Tests.push_back({"Follow payload snapshot and input preservation", &TestFollowPayloadSnapshotAndInputPreservation});
    }

    void RegisterFollowPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Follow coordinator first admission Auto Pumps", &TestFollowCoordinatorFirstAdmissionAutoPumps});
        Tests.push_back({"Follow coordinator second admission while busy", &TestFollowCoordinatorSecondAdmissionWhileBusy});
        Tests.push_back({"Follow coordinator rejects disabled flow", &TestFollowCoordinatorRejectsDisabledFlow});
        Tests.push_back({"Follow capacity rejection preserves prior admission", &TestFollowCapacityRejectionPreservesPriorAdmission});
        Tests.push_back({"Pending ready family inspection is non-consuming", &TestPendingReadyFamilyInspectionIsNonConsuming});
        Tests.push_back({"Wrong-family Begin preserves pending ready", &TestWrongFamilyBeginPreservesPendingReady});
        Tests.push_back({"Authorized Follow ready produces owned one-shot dispatch", &TestAuthorizedFollowReadyProducesOwnedOneShotDispatch});
        Tests.push_back({"Successful Follow completion cleans terminal state", &TestSuccessfulFollowCompletionCleansTerminalState});
        Tests.push_back({"Successful Follow completion captures Chat ready", &TestSuccessfulFollowCompletionCapturesChatReady});
        Tests.push_back({"Cancelled and Failed Follow completion use cancellation terminal", &TestCancelledAndFailedFollowCompletionUseCancellationTerminal});
        Tests.push_back({"Follow expiration cleans Discard and Consolidate payloads", &TestFollowExpirationCleansDiscardAndConsolidatePayloads});
        Tests.push_back({"Mixed Chat and Follow lifecycle preserves core order", &TestMixedChatAndFollowLifecyclePreservesCoreOrder});
    }
}
