#include "EventPipeline/Families/TSMemberFamily.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using namespace TikStudio::Tests;
    using namespace std::chrono_literals;

    [[nodiscard]]
    FTSMemberInput MakeMemberFamilyInput(
        std::int32_t ActionId,
        const std::string& Label
    )
    {
        FTSMemberInput Input;
        Input.ActionId = ActionId;
        Input.User.UniqueId = Label + "-user";
        Input.User.Nickname = Label + " nickname";
        Input.User.ProfilePictureUrl =
            "https://example.test/member-user.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = false;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    void RequireDirectMemberCandidate(
        const TTSFamilyDecision<FTSMemberPayload>& Decision,
        const FTSMemberInput& ExpectedInput,
        const std::string& Context
    )
    {
        Require(Decision.has_value(), Context + ": candidate expected");
        const TTSAdmissionCandidate<FTSMemberPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Member,
            Context + ": FamilyKind"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Member,
            Context + ": direct flow"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            Context + ": PriorityAdjustment"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL &&
                Candidate.EnqueueRequest.TTLOverride ==
                    std::chrono::milliseconds{0},
            Context + ": TTL defaults"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            Context + ": eviction protection"
        );
        RequireMemberInputEqual(
            Candidate.Payload.Input,
            ExpectedInput,
            Context + ": payload"
        );
    }

    void TestMemberProducesDirectMemberCandidate()
    {
        const FTSMemberInput Input = MakeMemberFamilyInput(73, "direct");
        const TTSFamilyDecision<FTSMemberPayload> Decision =
            FTSMemberFamily::Decide(Input);

        RequireDirectMemberCandidate(
            Decision,
            Input,
            "Member direct candidate"
        );
    }

    void TestMemberMetadataDoesNotActivateMemberRate()
    {
        const FTSMemberInput First = MakeMemberFamilyInput(0, "first");
        const FTSMemberInput Second = MakeMemberFamilyInput(
            std::numeric_limits<std::int32_t>::max(),
            "second"
        );

        const TTSFamilyDecision<FTSMemberPayload> FirstDecision =
            FTSMemberFamily::Decide(First);
        const TTSFamilyDecision<FTSMemberPayload> SecondDecision =
            FTSMemberFamily::Decide(Second);

        RequireDirectMemberCandidate(
            FirstDecision,
            First,
            "First independent Member decision"
        );
        RequireDirectMemberCandidate(
            SecondDecision,
            Second,
            "Second independent Member decision"
        );
        Require(
            FirstDecision->EnqueueRequest.Flow !=
                    ETSEventFlow::MemberRate &&
                SecondDecision->EnqueueRequest.Flow !=
                    ETSEventFlow::MemberRate,
            "Member metadata must not select MemberRate"
        );
        RequireMemberInputEqual(
            FirstDecision->Payload.Input,
            First,
            "First decision remains independent"
        );
    }

    [[nodiscard]]
    FTSEmissionId RequireAcceptedMemberAdmission(
        const FTSPipelineAdmissionResult& Admission,
        const std::string& Context
    )
    {
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            Context + ": admission"
        );
        const FTSEmissionEnvelope& Envelope =
            Admission.EnqueueResult->AdmittedEmission;
        Require(Envelope.EmissionId != 0, Context + ": identity");
        Require(
            Envelope.Flow == ETSEventFlow::Member &&
                Envelope.Flow != ETSEventFlow::MemberRate,
            Context + ": Member flow"
        );
        return Envelope.EmissionId;
    }

    void RequireLiveMemberAuthorities(
        const FTSEventPipelineCoordinator& Coordinator,
        FTSEmissionId EmissionId,
        const FTSMemberInput& ExpectedInput,
        ETSExternalEmissionState ExpectedState,
        std::size_t ExpectedBindingCount,
        std::size_t ExpectedMemberPayloadCount,
        std::size_t ExpectedChatPayloadCount,
        const std::string& Context
    )
    {
        Require(
            Coordinator.GetBindingCount() == ExpectedBindingCount &&
                Coordinator.GetMemberPayloadCount() ==
                    ExpectedMemberPayloadCount &&
                Coordinator.GetChatPayloadCount() ==
                    ExpectedChatPayloadCount &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            Context + ": authority counts"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [&](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.EmissionId == EmissionId &&
                            Binding.FamilyKind ==
                                ETSEventFamilyKind::Member &&
                            Binding.ExpectedFlow ==
                                ETSEventFlow::Member &&
                            Binding.ExpectedFlow !=
                                ETSEventFlow::MemberRate &&
                            Binding.PayloadHandle.Value != 0 &&
                            Binding.ExternalState == ExpectedState,
                        Context + ": binding"
                    );
                }
            ),
            Context + ": binding available"
        );
        Require(
            Coordinator.VisitMemberPayloadForEmission(
                EmissionId,
                [&](const FTSMemberPayload& Payload)
                {
                    RequireMemberInputEqual(
                        Payload.Input,
                        ExpectedInput,
                        Context + ": payload"
                    );
                }
            ),
            Context + ": payload available"
        );
    }

    void RequireMemberRemoved(
        const FTSEventPipelineCoordinator& Coordinator,
        FTSEmissionId EmissionId,
        std::size_t ExpectedBindingCount,
        std::size_t ExpectedChatPayloadCount,
        const std::string& Context
    )
    {
        Require(
            !Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding&)
                {
                }
            ) &&
                !Coordinator.VisitMemberPayloadForEmission(
                    EmissionId,
                    [](const FTSMemberPayload&)
                    {
                    }
                ) &&
                Coordinator.GetBindingCount() == ExpectedBindingCount &&
                Coordinator.GetMemberPayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() ==
                    ExpectedChatPayloadCount &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            Context + ": Member authorities removed"
        );
    }

    const FTSMemberProcessingDispatch& RequireMemberDispatch(
        const FTSMemberDispatchResult& Result,
        FTSEmissionId ExpectedEmissionId,
        const FTSMemberInput& ExpectedInput,
        const std::string& Context
    )
    {
        Require(
            Result.Status == ETSPipelineDispatchStatus::Dispatched &&
                Result.Dispatch.has_value(),
            Context + ": dispatch"
        );
        const FTSMemberProcessingDispatch& Dispatch = *Result.Dispatch;
        Require(
            Dispatch.Emission.EmissionId == ExpectedEmissionId &&
                Dispatch.Emission.Flow == ETSEventFlow::Member &&
                Dispatch.Emission.Flow != ETSEventFlow::MemberRate,
            Context + ": dispatch route"
        );
        RequireMemberInputEqual(
            Dispatch.Payload.Input,
            ExpectedInput,
            Context + ": dispatch payload"
        );
        return Dispatch;
    }

    void RequireSuccessfulMemberCompletion(
        const FTSMemberProcessingCompletionResult& Completion,
        FTSEmissionId EmissionId,
        const std::string& Context
    )
    {
        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult ==
                    ETSProcessingResult::Succeeded &&
                Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            Context + ": completion result"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed &&
                !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.Flow == ETSEventFlow::Member &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.Flow != ETSEventFlow::MemberRate &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            Context + ": confirmation lifecycle"
        );
    }

    void TestMemberFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSMemberInput ExpectedInput = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitMember(ExpectedInput);
        const FTSEmissionId EmissionId = RequireAcceptedMemberAdmission(
            Admission,
            "First Member"
        );
        Require(
            Admission.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Admission.EnqueueResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == EmissionId &&
                Admission.EnqueueResult->AutoPumpOutcome.ReadyEmission.Flow ==
                    ETSEventFlow::Member &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Member,
            "First Member must Auto Pump"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            EmissionId,
            ExpectedInput,
            ETSExternalEmissionState::Bound,
            1,
            1,
            0,
            "First Member Bound"
        );

        const FTSMemberDispatchResult Dispatch =
            Coordinator.BeginMemberProcessing();
        (void)RequireMemberDispatch(
            Dispatch,
            EmissionId,
            ExpectedInput,
            "First Member"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            EmissionId,
            ExpectedInput,
            ETSExternalEmissionState::Processing,
            1,
            1,
            0,
            "First Member Processing"
        );

        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            Completion,
            EmissionId,
            "First Member"
        );
        RequireMemberRemoved(Coordinator, EmissionId, 0, 0, "First Member");
        Require(
            !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "First Member must leave no ready"
        );
    }

    void TestMemberSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalMemberSettings(true, true)
        );
        FTSMemberInput FirstInput = MakeCompleteMemberInput();
        FirstInput.ActionId = 1;
        FirstInput.User.UniqueId = "first-member-user";
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitMember(FirstInput);
        const FTSEmissionId FirstId =
            RequireAcceptedMemberAdmission(First, "First busy Member");
        const FTSMemberDispatchResult FirstDispatch =
            Coordinator.BeginMemberProcessing();
        (void)RequireMemberDispatch(
            FirstDispatch,
            FirstId,
            FirstInput,
            "First busy Member"
        );

        FTSMemberInput SecondInput = MakeCompleteMemberInput();
        SecondInput.ActionId = 2;
        SecondInput.User.UniqueId = "second-member-user";
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitMember(SecondInput);
        const FTSEmissionId SecondId =
            RequireAcceptedMemberAdmission(Second, "Second busy Member");
        Require(
            Second.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Second Member must remain Pending while Core is busy"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            FirstId,
            FirstInput,
            ETSExternalEmissionState::Processing,
            2,
            2,
            0,
            "First busy Member Processing"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            SecondId,
            SecondInput,
            ETSExternalEmissionState::Bound,
            2,
            2,
            0,
            "Second busy Member Pending"
        );

        const FTSMemberProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteMemberProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            FirstCompletion,
            FirstId,
            "First busy Member"
        );
        Require(
            FirstCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                FirstCompletion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == SecondId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Member,
            "First completion must expose second Member"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            SecondId,
            SecondInput,
            ETSExternalEmissionState::Bound,
            1,
            1,
            0,
            "Second Member ready"
        );
        Require(
            BeginReadyMember(Coordinator) == SecondId,
            "Second Member must enter Processing"
        );
        const FTSMemberProcessingCompletionResult SecondCompletion =
            Coordinator.CompleteMemberProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            SecondCompletion,
            SecondId,
            "Second busy Member"
        );
        RequireMemberRemoved(
            Coordinator,
            SecondId,
            0,
            0,
            "Second busy Member"
        );
    }

    void TestDisabledMemberRejectsWithoutLeaks()
    {
        FTSEventPipelineCoordinator Coordinator(MakeMemberSettings(false, 1));
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitMember(MakeCompleteMemberInput());

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Admission.EnqueueResult.has_value() &&
                Admission.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Disabled Member must reject"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetMemberPayloadCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Disabled Member must not leak authorities"
        );
    }

    void TestMemberCapacityRejectionRemovesProvisionalPayload()
    {
        FTSEventQueueSettings Settings = MakeMemberSettings(true, 1);
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        FTSEventPipelineCoordinator Coordinator(std::move(Settings));

        const FTSMemberInput FirstInput = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitMember(FirstInput);
        const FTSEmissionId FirstId =
            RequireAcceptedMemberAdmission(First, "Capacity Member");

        FTSMemberInput RejectedInput = MakeCompleteMemberInput();
        RejectedInput.ActionId = 99;
        RejectedInput.User.UniqueId = "rejected-member-user";
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitMember(std::move(RejectedInput));
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedAtCapacity,
            "Second Member must reject at capacity"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            FirstId,
            FirstInput,
            ETSExternalEmissionState::Bound,
            1,
            1,
            0,
            "Preserved capacity Member"
        );
        Require(
            !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Capacity Member must await explicit Pump"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == FirstId &&
                PumpResult.Outcome.ReadyEmission.Flow ==
                    ETSEventFlow::Member &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Member,
            "Explicit Pump must select preserved Member"
        );
        Require(
            BeginReadyMember(Coordinator) == FirstId,
            "Preserved Member must enter Processing"
        );
        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(Completion, FirstId, "Capacity Member");
        RequireMemberRemoved(Coordinator, FirstId, 0, 0, "Capacity Member");
    }

    void TestMemberDispatchOwnsSnapshotAndIsOneShot()
    {
        const auto MutateInput = [](FTSMemberInput& Input)
        {
            Input.ActionId = 0;
            Input.User.UniqueId = "mutated-member-user";
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
        FTSMemberInput Input = MakeCompleteMemberInput();
        const FTSMemberInput ExpectedInput = Input;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitMember(Input);
        const FTSEmissionId EmissionId = RequireAcceptedMemberAdmission(
            Admission,
            "Owned Member dispatch"
        );
        MutateInput(Input);

        FTSMemberDispatchResult Dispatch = Coordinator.BeginMemberProcessing();
        (void)RequireMemberDispatch(
            Dispatch,
            EmissionId,
            ExpectedInput,
            "Owned Member dispatch"
        );
        FTSMemberProcessingDispatch& OwnedDispatch = *Dispatch.Dispatch;
        MutateInput(OwnedDispatch.Payload.Input);
        RequireLiveMemberAuthorities(
            Coordinator,
            EmissionId,
            ExpectedInput,
            ETSExternalEmissionState::Processing,
            1,
            1,
            0,
            "Stored Member after dispatch mutation"
        );

        const FTSMemberDispatchResult SecondDispatch =
            Coordinator.BeginMemberProcessing();
        Require(
            SecondDispatch.Status ==
                    ETSPipelineDispatchStatus::NoEmissionReady &&
                !SecondDispatch.Dispatch.has_value() &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Member dispatch must consume ready exactly once"
        );
        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            Completion,
            EmissionId,
            "Owned Member dispatch"
        );
        RequireMemberRemoved(
            Coordinator,
            EmissionId,
            0,
            0,
            "Owned Member dispatch"
        );
    }

    void TestWrongFamilyBeginPreservesReadyMember()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSMemberInput Input = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitMember(Input);
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(Admission, "Wrong Begin Member");

        const auto RequirePreserved = [&]()
        {
            Require(
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Member,
                "Wrong Begin must preserve ready Member"
            );
            RequireLiveMemberAuthorities(
                Coordinator,
                MemberId,
                Input,
                ETSExternalEmissionState::Bound,
                1,
                1,
                0,
                "Wrong Begin Member"
            );
        };
        const auto RequireNoDispatch = [&](const auto& Result)
        {
            Require(
                Result.Status ==
                        ETSPipelineDispatchStatus::NoEmissionReady &&
                    !Result.Dispatch.has_value(),
                "Wrong-family Begin must not consume Member"
            );
            RequirePreserved();
        };

        RequireNoDispatch(Coordinator.BeginChatProcessing());
        RequireNoDispatch(Coordinator.BeginFollowProcessing());
        RequireNoDispatch(Coordinator.BeginShareProcessing());
        RequireNoDispatch(Coordinator.BeginLikeProcessing());
        RequireNoDispatch(Coordinator.BeginRoomUserProcessing());
        RequireNoDispatch(Coordinator.BeginGiftProcessing());
        Require(
            BeginReadyMember(Coordinator) == MemberId,
            "Correct Member Begin must consume preserved ready"
        );
        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            Completion,
            MemberId,
            "Wrong Begin Member cleanup"
        );
        RequireMemberRemoved(
            Coordinator,
            MemberId,
            0,
            0,
            "Wrong Begin Member cleanup"
        );
    }

    void TestMemberSuccessCompletionConfirmsAndCleans()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSMemberInput Input = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitMember(Input);
        const FTSEmissionId MemberId =
            RequireAcceptedMemberAdmission(Admission, "Successful Member");
        Require(
            BeginReadyMember(Coordinator) == MemberId,
            "Successful Member must enter Processing"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            MemberId,
            Input,
            ETSExternalEmissionState::Processing,
            1,
            1,
            0,
            "Successful Member Processing"
        );

        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            Completion,
            MemberId,
            "Successful Member"
        );
        RequireMemberRemoved(
            Coordinator,
            MemberId,
            0,
            0,
            "Successful Member"
        );
        Require(
            !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Successful Member must leave no ready"
        );
    }

    void TestMemberCancelledAndFailedCleanWithoutRetry()
    {
        const auto RunTerminalScenario = [](
            ETSProcessingResult ProcessingResult
        )
        {
            FTSEventPipelineCoordinator Coordinator;
            FTSMemberInput Input = MakeCompleteMemberInput();
            Input.ActionId =
                ProcessingResult == ETSProcessingResult::Cancelled ? 31 : 32;
            Input.User.UniqueId =
                ProcessingResult == ETSProcessingResult::Cancelled
                    ? "cancelled-member-user"
                    : "failed-member-user";
            const FTSPipelineAdmissionResult Admission =
                Coordinator.SubmitMember(Input);
            const FTSEmissionId MemberId = RequireAcceptedMemberAdmission(
                Admission,
                "Terminal Member"
            );
            Require(
                BeginReadyMember(Coordinator) == MemberId,
                "Terminal Member must enter Processing"
            );
            RequireLiveMemberAuthorities(
                Coordinator,
                MemberId,
                Input,
                ETSExternalEmissionState::Processing,
                1,
                1,
                0,
                "Terminal Member Processing"
            );

            const FTSMemberProcessingCompletionResult Completion =
                Coordinator.CompleteMemberProcessing(
                    MemberId,
                    ProcessingResult
                );
            Require(
                Completion.EmissionId == MemberId &&
                    Completion.ProcessingResult == ProcessingResult &&
                    !Completion.ConfirmResult.has_value() &&
                    Completion.CancelResult.has_value(),
                "Cancelled or Failed Member result"
            );
            Require(
                Completion.CancelResult->Status ==
                        ETSCancelInFlightStatus::Cancelled &&
                    Completion.CancelResult->LifecycleEvents.size() == 1 &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.EmissionId == MemberId &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.Flow ==
                        ETSEventFlow::Member &&
                    Completion.CancelResult->LifecycleEvents.front()
                            .Envelope.Flow !=
                        ETSEventFlow::MemberRate &&
                    Completion.CancelResult->LifecycleEvents.front().Reason ==
                        ETSEmissionTerminalReason::Cancelled,
                "Cancelled or Failed Member lifecycle"
            );
            RequireMemberRemoved(
                Coordinator,
                MemberId,
                0,
                0,
                "Terminal Member"
            );
            Require(
                Coordinator.Pump().Outcome.Status ==
                    ETSPumpStatus::QueueEmpty,
                "Terminal Member must not retry"
            );
        };

        RunTerminalScenario(ETSProcessingResult::Cancelled);
        RunTerminalScenario(ETSProcessingResult::Failed);
    }

    void TestWrongFamilyCompletionPreservesInFlightBothDirections()
    {
        FTSEventPipelineCoordinator MemberCoordinator;
        const FTSMemberInput MemberInput = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult MemberAdmission =
            MemberCoordinator.SubmitMember(MemberInput);
        const FTSEmissionId MemberId = RequireAcceptedMemberAdmission(
            MemberAdmission,
            "Wrong completion Member"
        );
        Require(
            BeginReadyMember(MemberCoordinator) == MemberId,
            "Wrong completion Member must enter Processing"
        );

        const auto RequireMemberPreserved = [&]()
        {
            RequireLiveMemberAuthorities(
                MemberCoordinator,
                MemberId,
                MemberInput,
                ETSExternalEmissionState::Processing,
                1,
                1,
                0,
                "Wrong completion preserves Member"
            );
            Require(
                MemberCoordinator.Pump().Outcome.Status ==
                    ETSPumpStatus::Busy,
                "Member must remain InFlight"
            );
        };
        const auto RequireMemberRouteError = [&](auto&& Callback)
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
            Require(bThrew, "Earlier-family completion must reject Member");
            RequireMemberPreserved();
        };

        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteChatProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteFollowProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteShareProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteLikeProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteRoomUserProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        RequireMemberRouteError([&]()
        {
            (void)MemberCoordinator.CompleteGiftProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        });
        const FTSMemberProcessingCompletionResult MemberCompletion =
            MemberCoordinator.CompleteMemberProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            MemberCompletion,
            MemberId,
            "Correct Member completion"
        );
        RequireMemberRemoved(
            MemberCoordinator,
            MemberId,
            0,
            0,
            "Correct Member completion"
        );

        FTSEventPipelineCoordinator ChatCoordinator;
        const FTSEmissionId ChatId = SubmitAcceptedChat(
            ChatCoordinator,
            "Chat protected from Member completion"
        );
        Require(
            BeginReadyChat(ChatCoordinator) == ChatId,
            "Chat must enter Processing"
        );
        bool bMemberCompletionThrew = false;
        try
        {
            (void)ChatCoordinator.CompleteMemberProcessing(
                ChatId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bMemberCompletionThrew = true;
        }
        Require(
            bMemberCompletionThrew &&
                ChatCoordinator.GetBindingCount() == 1 &&
                ChatCoordinator.GetChatPayloadCount() == 1 &&
                ChatCoordinator.GetMemberPayloadCount() == 0 &&
                ChatCoordinator.VisitEmissionBinding(
                    ChatId,
                    [](const FTSEmissionBinding& Binding)
                    {
                        Require(
                            Binding.FamilyKind == ETSEventFamilyKind::Chat &&
                                Binding.ExpectedFlow == ETSEventFlow::Chat &&
                                Binding.ExternalState ==
                                    ETSExternalEmissionState::Processing,
                            "Member completion must preserve Chat InFlight"
                        );
                    }
                ) &&
                ChatCoordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Member completion must reject another-family InFlight"
        );
        (void)ChatCoordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            ChatCoordinator.GetBindingCount() == 0 &&
                ChatCoordinator.GetChatPayloadCount() == 0 &&
                ChatCoordinator.GetMemberPayloadCount() == 0 &&
                ChatCoordinator.GetFollowPayloadCount() == 0 &&
                ChatCoordinator.GetSharePayloadCount() == 0 &&
                ChatCoordinator.GetLikePayloadCount() == 0 &&
                ChatCoordinator.GetRoomUserPayloadCount() == 0 &&
                ChatCoordinator.GetGiftPayloadCount() == 0,
            "Wrong completion reverse direction must clean Chat"
        );
    }

    void TestMemberCompletionCapturesReadyChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSMemberInput MemberInput = MakeCompleteMemberInput();
        const FTSPipelineAdmissionResult MemberAdmission =
            Coordinator.SubmitMember(MemberInput);
        const FTSEmissionId MemberId = RequireAcceptedMemberAdmission(
            MemberAdmission,
            "Member before Chat"
        );
        Require(
            BeginReadyMember(Coordinator) == MemberId,
            "Member must enter Processing before Chat"
        );

        FTSChatInput ChatInput = MakeCompleteInput();
        ChatInput.Comment = "Chat after Member";
        const FTSPipelineAdmissionResult ChatAdmission =
            Coordinator.SubmitChat(ChatInput);
        Require(
            ChatAdmission.Status == ETSPipelineAdmissionStatus::Accepted &&
                ChatAdmission.EnqueueResult.has_value() &&
                ChatAdmission.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Chat after Member must remain Pending"
        );
        const FTSEmissionId ChatId =
            ChatAdmission.EnqueueResult->AdmittedEmission.EmissionId;
        RequireLiveMemberAuthorities(
            Coordinator,
            MemberId,
            MemberInput,
            ETSExternalEmissionState::Processing,
            2,
            1,
            1,
            "Member before Chat Processing"
        );

        const FTSMemberProcessingCompletionResult Completion =
            Coordinator.CompleteMemberProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            Completion,
            MemberId,
            "Member before Chat"
        );
        Require(
            Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == ChatId &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Chat,
            "Member completion must capture pending Chat"
        );
        RequireMemberRemoved(
            Coordinator,
            MemberId,
            1,
            1,
            "Member before Chat"
        );

        const FTSChatDispatchResult ChatDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            ChatDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                ChatDispatch.Dispatch.has_value() &&
                ChatDispatch.Dispatch->Emission.EmissionId == ChatId,
            "Chat selected after Member must dispatch"
        );
        RequireChatInputEqual(
            ChatDispatch.Dispatch->Payload.Input,
            ChatInput,
            "Chat after Member payload"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetMemberPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            "Member then Chat must clean authorities"
        );
    }

    void TestChatCompletionCapturesReadyMember()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId ChatId = SubmitAcceptedChat(
            Coordinator,
            "Chat before Member"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Member"
        );

        FTSMemberInput MemberInput = MakeCompleteMemberInput();
        MemberInput.ActionId = 91;
        MemberInput.User.UniqueId = "member-after-chat-user";
        const FTSPipelineAdmissionResult MemberAdmission =
            Coordinator.SubmitMember(MemberInput);
        const FTSEmissionId MemberId = RequireAcceptedMemberAdmission(
            MemberAdmission,
            "Member after Chat"
        );
        Require(
            MemberAdmission.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::NotRequested &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Member after Chat must remain Pending"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            MemberId,
            MemberInput,
            ETSExternalEmissionState::Bound,
            2,
            1,
            1,
            "Member after Chat Pending"
        );

        const FTSChatProcessingCompletionResult ChatCompletion =
            Coordinator.CompleteChatProcessing(
                ChatId,
                ETSProcessingResult::Succeeded
            );
        Require(
            ChatCompletion.ConfirmResult.has_value() &&
                ChatCompletion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                ChatCompletion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == MemberId &&
                ChatCompletion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .Flow == ETSEventFlow::Member &&
                Coordinator.PeekPendingReadyFamilyKind() ==
                    ETSEventFamilyKind::Member,
            "Chat completion must capture pending Member"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            MemberId,
            MemberInput,
            ETSExternalEmissionState::Bound,
            1,
            1,
            0,
            "Member ready after Chat"
        );

        const FTSMemberDispatchResult MemberDispatch =
            Coordinator.BeginMemberProcessing();
        (void)RequireMemberDispatch(
            MemberDispatch,
            MemberId,
            MemberInput,
            "Member after Chat"
        );
        const FTSMemberProcessingCompletionResult MemberCompletion =
            Coordinator.CompleteMemberProcessing(
                MemberId,
                ETSProcessingResult::Succeeded
            );
        RequireSuccessfulMemberCompletion(
            MemberCompletion,
            MemberId,
            "Member after Chat"
        );
        RequireMemberRemoved(
            Coordinator,
            MemberId,
            0,
            0,
            "Member after Chat"
        );
    }

    void TestPendingMemberExpiresWhileChatIsProcessing()
    {
        FControlledClock Clock;
        FTSEventQueueSettings Settings = MakeOperationalMemberSettings(
            true,
            true,
            5s,
            ETSEventExpirePolicy::Discard
        );
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Expiration Chat settings");
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
            "Processing while Member expires"
        );
        Require(
            BeginReadyChat(Coordinator) == ChatId,
            "Chat must enter Processing before Member"
        );
        FTSMemberInput MemberInput = MakeCompleteMemberInput();
        MemberInput.User.UniqueId = "expiring-member-user";
        const FTSPipelineAdmissionResult MemberAdmission =
            Coordinator.SubmitMember(MemberInput);
        const FTSEmissionId MemberId = RequireAcceptedMemberAdmission(
            MemberAdmission,
            "Expiring Member"
        );
        RequireLiveMemberAuthorities(
            Coordinator,
            MemberId,
            MemberInput,
            ETSExternalEmissionState::Bound,
            2,
            1,
            1,
            "Expiring Member Pending"
        );
        Require(
            !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Pending Member must not be ready while Chat is Processing"
        );

        Clock.Advance(6s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents.front().Envelope.EmissionId ==
                    MemberId &&
                Expirations.LifecycleEvents.front().Envelope.Flow ==
                    ETSEventFlow::Member &&
                Expirations.LifecycleEvents.front().Envelope.Flow !=
                    ETSEventFlow::MemberRate &&
                Expirations.LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Pending Member expiration lifecycle"
        );
        RequireMemberRemoved(
            Coordinator,
            MemberId,
            1,
            1,
            "Expired Member"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                ChatId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Chat &&
                            Binding.ExpectedFlow == ETSEventFlow::Chat &&
                            Binding.ExternalState ==
                                ETSExternalEmissionState::Processing,
                        "Member expiration must preserve Chat Processing"
                    );
                }
            ) &&
                Coordinator.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Core must remain busy with Chat after Member expiration"
        );
        (void)Coordinator.CompleteChatProcessing(
            ChatId,
            ETSProcessingResult::Succeeded
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0 &&
                Coordinator.GetMemberPayloadCount() == 0 &&
                Coordinator.GetFollowPayloadCount() == 0 &&
                Coordinator.GetSharePayloadCount() == 0 &&
                Coordinator.GetLikePayloadCount() == 0 &&
                Coordinator.GetRoomUserPayloadCount() == 0 &&
                Coordinator.GetGiftPayloadCount() == 0,
            "Member expiration scenario must clean Chat"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterMemberPipelineFamilyTests(FTSTestCases& Tests)
    {
        Tests.push_back({
            "Member produces direct Member candidate",
            &TestMemberProducesDirectMemberCandidate
        });
        Tests.push_back({
            "Member metadata does not activate MemberRate",
            &TestMemberMetadataDoesNotActivateMemberRate
        });
    }

    void RegisterMemberPipelineCoordinatorTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Member first admission Auto Pumps", &TestMemberFirstAdmissionAutoPumps});
        Tests.push_back({"Member second admission while busy", &TestMemberSecondAdmissionWhileBusy});
        Tests.push_back({"Disabled Member rejects without leaks", &TestDisabledMemberRejectsWithoutLeaks});
        Tests.push_back({"Member capacity rejection removes provisional payload", &TestMemberCapacityRejectionRemovesProvisionalPayload});
        Tests.push_back({"Member dispatch owns snapshot and is one shot", &TestMemberDispatchOwnsSnapshotAndIsOneShot});
        Tests.push_back({"Wrong-family Begin preserves ready Member", &TestWrongFamilyBeginPreservesReadyMember});
        Tests.push_back({"Member success completion confirms and cleans", &TestMemberSuccessCompletionConfirmsAndCleans});
        Tests.push_back({"Member Cancelled and Failed clean without retry", &TestMemberCancelledAndFailedCleanWithoutRetry});
        Tests.push_back({"Wrong-family completion preserves InFlight both directions", &TestWrongFamilyCompletionPreservesInFlightBothDirections});
        Tests.push_back({"Member completion captures ready Chat", &TestMemberCompletionCapturesReadyChat});
        Tests.push_back({"Chat completion captures ready Member", &TestChatCompletionCapturesReadyMember});
        Tests.push_back({"Pending Member expires while Chat is Processing", &TestPendingMemberExpiresWhileChatIsProcessing});
    }
}
