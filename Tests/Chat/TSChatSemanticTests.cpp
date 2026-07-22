#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Priority/TSCommonUserPriorityPolicy.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"
#include "EventPipeline/State/TSChatPendingBatchIndex.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using namespace std::chrono_literals;
    using namespace TikStudio::Tests;

    [[nodiscard]]
    FTSEventQueueSettings MakePendingCoreSettings(
        std::chrono::milliseconds TTL = 30s,
        std::uint32_t MaxSlots = 10
    )
    {
        FTSEventQueueSettings Settings = MakeOperationalChatSettings(
            false,
            true,
            TTL,
            ETSEventExpirePolicy::Discard
        );
        FTSFlowQueueSettings* Chat =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(Chat != nullptr, "Pending Chat settings");
        Chat->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    FTSChatInput MakeSemanticInput(
        std::string UserUniqueId,
        std::string Comment
    )
    {
        FTSChatInput Input = MakeCompleteInput();
        Input.User.UniqueId = std::move(UserUniqueId);
        Input.Comment = std::move(Comment);
        return Input;
    }

    [[nodiscard]]
    FTSChatPayload ReadChatPayload(
        const FTSEventPipelineCoordinator& Coordinator,
        FTSEmissionId EmissionId
    )
    {
        FTSChatPayload Payload;
        Require(
            Coordinator.VisitChatPayloadForEmission(
                EmissionId,
                [&](const FTSChatPayload& StoredPayload)
                {
                    Payload = StoredPayload;
                }
            ),
            "Semantic Chat payload must exist"
        );
        return Payload;
    }

    void TestChatSemanticSettingsDefaultsAndValidCustomSettings()
    {
        const FTSChatSemanticSettings Defaults;
        Require(!Defaults.bOnlyAllowCommands, "Default command-only mode");
        Require(Defaults.CommandPrefix == "!", "Default command prefix");
        Require(Defaults.bAllowLeadingWhitespace, "Default leading whitespace");
        Require(!Defaults.bRequireCommandBoundary, "Default boundary");
        Require(Defaults.MaxCommandPrefixUtf8Bytes == 16, "Default prefix bytes");
        Require(Defaults.MaxMessagesPerBatch == 8, "Default message count");
        Require(Defaults.MaxMessageUtf8Bytes == 1024, "Default message bytes");
        Require(Defaults.MaxBatchUtf8Bytes == 4096, "Default batch bytes");
        Require(AreChatSemanticSettingsValid(Defaults), "Defaults must be valid");

        FTSChatSemanticSettings Custom = Defaults;
        Custom.bOnlyAllowCommands = true;
        Custom.CommandPrefix = "/t";
        Custom.bRequireCommandBoundary = true;
        Custom.MaxCommandPrefixUtf8Bytes = 2;
        Custom.MaxMessagesPerBatch = 2;
        Custom.MaxMessageUtf8Bytes = 32;
        Custom.MaxBatchUtf8Bytes = 64;
        Require(AreChatSemanticSettingsValid(Custom), "Custom settings must be valid");
    }

    void TestChatSemanticSettingsRejectInvalidPrefixes()
    {
        FTSChatSemanticSettings Settings;
        Settings.CommandPrefix.clear();
        Require(!AreChatSemanticSettingsValid(Settings), "Empty prefix must fail");

        Settings = {};
        Settings.MaxCommandPrefixUtf8Bytes = 1;
        Settings.CommandPrefix = "/t";
        Require(!AreChatSemanticSettingsValid(Settings), "Long prefix must fail");

        const std::array<std::string, 3> InvalidPrefixes{
            std::string("a b"),
            std::string("\t"),
            std::string("a\0b", 3)
        };
        for (const std::string& Prefix : InvalidPrefixes)
        {
            Settings = {};
            Settings.CommandPrefix = Prefix;
            Require(
                !AreChatSemanticSettingsValid(Settings),
                "Whitespace or control prefix must fail"
            );
        }
    }

    void TestChatSemanticSettingsRejectZeroLimitsAndConstruction()
    {
        FTSChatSemanticSettings Settings;
        Settings.MaxCommandPrefixUtf8Bytes = 0;
        Require(!AreChatSemanticSettingsValid(Settings), "Zero prefix bytes must fail");
        Settings = {};
        Settings.MaxMessagesPerBatch = 0;
        Require(!AreChatSemanticSettingsValid(Settings), "Zero messages must fail");
        Settings = {};
        Settings.MaxMessageUtf8Bytes = 0;
        Require(!AreChatSemanticSettingsValid(Settings), "Zero message bytes must fail");
        Settings = {};
        Settings.MaxBatchUtf8Bytes = 0;
        Require(!AreChatSemanticSettingsValid(Settings), "Zero batch bytes must fail");
        Settings = {};
        Settings.MaxBatchUtf8Bytes = Settings.MaxMessageUtf8Bytes - 1;
        Require(!AreChatSemanticSettingsValid(Settings), "Small batch must fail");

        FTSEventPipelineSettings PipelineSettings;
        PipelineSettings.Chat.CommandPrefix.clear();
        bool bThrew = false;
        try
        {
            FTSEventPipelineCoordinator Coordinator(
                {},
                {},
                PipelineSettings
            );
            (void)Coordinator;
        }
        catch (const std::invalid_argument&)
        {
            bThrew = true;
        }
        Require(bThrew, "Coordinator must reject invalid semantic settings");

        PipelineSettings = {};
        PipelineSettings.CommonUserPriority.ModeratorBonus = -1;
        bThrew = false;
        try
        {
            FTSEventPipelineCoordinator Coordinator(
                {},
                {},
                PipelineSettings
            );
            (void)Coordinator;
        }
        catch (const std::invalid_argument&)
        {
            bThrew = true;
        }
        Require(bThrew, "Coordinator must reject invalid priority settings");
    }

    void TestChatCommandClassificationRules()
    {
        FTSChatSemanticSettings Settings;
        Require(FTSChatFamily::IsCommand("!saltar", Settings), "! command");
        Require(FTSChatFamily::IsCommand("  !saltar", Settings), "leading spaces");
        Require(!FTSChatFamily::IsCommand("!SALTAR", FTSChatSemanticSettings{
            false, "!saltar", true, false, 16, 8, 1024, 4096
        }), "Command comparison must be case-sensitive");

        Settings.bAllowLeadingWhitespace = false;
        Require(!FTSChatFamily::IsCommand(" !saltar", Settings), "spaces disabled");

        Settings = {};
        Settings.CommandPrefix = "/t";
        Settings.bRequireCommandBoundary = true;
        Require(FTSChatFamily::IsCommand("/t", Settings), "boundary at end");
        Require(FTSChatFamily::IsCommand("/t Hola.", Settings), "space boundary");
        Require(!FTSChatFamily::IsCommand("/tarde", Settings), "missing boundary");
    }

    void TestChatCommandModesAndFilteredInputDoNotMutateBatch()
    {
        FTSEventPipelineSettings General;
        FTSEventPipelineCoordinator GeneralCoordinator(
            MakePendingCoreSettings(),
            {},
            General
        );
        Require(
            GeneralCoordinator.SubmitChat(
                MakeSemanticInput("general-user", "hello")
            ).Status == ETSPipelineAdmissionStatus::Accepted,
            "General mode must accept normal messages"
        );
        FTSChatInput Invalid = MakeSemanticInput("", "valid-sized message");
        const FTSPipelineAdmissionResult InvalidResult =
            GeneralCoordinator.SubmitChat(std::move(Invalid));
        Require(
            InvalidResult.Status ==
                ETSPipelineAdmissionStatus::RejectedInvalidInput &&
                InvalidResult.AffectedEmissionId == 0 &&
                !InvalidResult.EnqueueResult.has_value(),
            "Empty user identity must be rejected before Core admission"
        );

        FTSEventPipelineSettings CommandsOnly;
        CommandsOnly.Chat.bOnlyAllowCommands = true;
        FTSEventPipelineCoordinator Coordinator(
            MakePendingCoreSettings(),
            {},
            CommandsOnly
        );
        const FTSPipelineAdmissionResult Accepted = Coordinator.SubmitChat(
            MakeSemanticInput("command-user", "!first")
        );
        Require(Accepted.Status == ETSPipelineAdmissionStatus::Accepted, "Command accepted");
        const FTSChatPayload Before = ReadChatPayload(
            Coordinator,
            Accepted.AffectedEmissionId
        );

        const FTSPipelineAdmissionResult Filtered = Coordinator.SubmitChat(
            MakeSemanticInput("command-user", "normal")
        );
        Require(
            Filtered.Status == ETSPipelineAdmissionStatus::NoEmission &&
                Filtered.AffectedEmissionId == 0 &&
                !Filtered.EnqueueResult.has_value(),
            "Command-only mode must filter normal messages before admission"
        );
        const FTSChatPayload After = ReadChatPayload(
            Coordinator,
            Accepted.AffectedEmissionId
        );
        Require(After.Messages.size() == Before.Messages.size(), "Filtered append count");
        Require(After.Messages[0].Comment == "!first", "Filtered append payload");
    }

    void TestChatInitialPayloadTimestampCommandAndPriority()
    {
        FControlledClock Clock;
        FTSEventQueueSettings CoreSettings = MakePendingCoreSettings();
        FTSFlowQueueSettings* Chat =
            CoreSettings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(Chat != nullptr, "Chat flow settings");
        Chat->BaseWeight = 7;

        FTSEventPipelineCoordinator Coordinator(
            CoreSettings,
            Clock.MakeProvider()
        );
        FTSChatInput Input = MakeSemanticInput("priority-user", "!go");
        Input.User.FollowRole = 2;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 1;
        Input.User.GifterLevel = 50;
        Input.User.TeamMemberLevel = 50;

        const FTSPipelineAdmissionResult Admission = Coordinator.SubmitChat(Input);
        Require(Admission.Status == ETSPipelineAdmissionStatus::Accepted, "Priority admission");
        const FTSChatPayload Payload = ReadChatPayload(
            Coordinator,
            Admission.AffectedEmissionId
        );
        RequireChatPayloadMatchesInput(Payload, Input, "Initial semantic payload");
        Require(Payload.Messages[0].ReceivedAt == Clock.Now, "ReceivedAt capture");
        Require(Payload.Messages[0].bIsCommand, "Per-message command flag");
        Require(Payload.CommonPriorityAdjustment == 170, "Maximum common priority");
        Require(
            Admission.EnqueueResult->AdmittedEmission.PriorityScore == 177,
            "Core priority must be base plus common adjustment"
        );
    }

    void TestChatAccumulationPreservesIdentityOrderAndFrozenPriority()
    {
        const FTSEventPipelineSettings PipelineSettings;
        FTSEventPipelineCoordinator Coordinator(
            MakePendingCoreSettings(),
            {},
            PipelineSettings
        );
        FTSChatInput First = MakeSemanticInput("batch-user", "one");
        First.User.bIsModerator = true;
        const std::int64_t FrozenPriority =
            FTSCommonUserPriorityPolicy::Evaluate(
                First.User,
                PipelineSettings.CommonUserPriority
            ).TotalAdjustment;
        const FTSPipelineAdmissionResult Admission = Coordinator.SubmitChat(First);
        Require(Admission.Status == ETSPipelineAdmissionStatus::Accepted, "Initial batch");

        FTSChatInput Second = MakeSemanticInput("batch-user", "two");
        Second.User.Nickname = "latest nickname";
        Second.User.bIsModerator = false;
        Second.User.bIsSubscriber = true;
        const FTSPipelineAdmissionResult Accumulated = Coordinator.SubmitChat(Second);
        FTSChatInput ThirdInput = MakeSemanticInput("batch-user", "three");
        ThirdInput.User.Nickname = "final latest nickname";
        const FTSPipelineAdmissionResult Third =
            Coordinator.SubmitChat(ThirdInput);
        Require(
            Accumulated.Status == ETSPipelineAdmissionStatus::Accumulated &&
                Third.Status == ETSPipelineAdmissionStatus::Accumulated &&
                Accumulated.AffectedEmissionId == Admission.AffectedEmissionId &&
                !Accumulated.EnqueueResult.has_value(),
            "Same pending user must retain one emission identity"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                Coordinator.GetPendingChatBatchCount() == 1,
            "Accumulation must not create another authority entry"
        );

        const FTSChatPayload Payload = ReadChatPayload(
            Coordinator,
            Admission.AffectedEmissionId
        );
        Require(Payload.Messages.size() == 3, "Three messages must remain separate");
        Require(
            Payload.Messages[0].Comment == "one" &&
                Payload.Messages[1].Comment == "two" &&
                Payload.Messages[2].Comment == "three",
            "Accumulated messages must preserve order"
        );
        Require(
            Payload.CommonPriorityAdjustment == FrozenPriority,
            "Common priority must remain frozen from the first user snapshot"
        );
        Require(
            Payload.User.Nickname == "final latest nickname",
            "Latest accepted user snapshot must replace the previous snapshot"
        );
        Coordinator.ValidateInternalConsistency();
    }

    void TestChatDifferentUsersAndReadySuccessorRemainSeparate()
    {
        FTSEventPipelineCoordinator PendingCoordinator(MakePendingCoreSettings());
        const FTSPipelineAdmissionResult A = PendingCoordinator.SubmitChat(
            MakeSemanticInput("user-a", "a")
        );
        const FTSPipelineAdmissionResult B = PendingCoordinator.SubmitChat(
            MakeSemanticInput("user-b", "b")
        );
        Require(
            A.Status == ETSPipelineAdmissionStatus::Accepted &&
                B.Status == ETSPipelineAdmissionStatus::Accepted &&
                A.AffectedEmissionId != B.AffectedEmissionId &&
                PendingCoordinator.GetPendingChatBatchCount() == 2,
            "Different users must create separate pending batches"
        );
        const FTSPumpResult Pumped = PendingCoordinator.Pump();
        Require(
            Pumped.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PendingCoordinator.GetPendingChatBatchCount() == 1,
            "Pump selection must close exactly the selected mutable batch"
        );
        const FTSPipelineAdmissionResult PumpSuccessor =
            PendingCoordinator.SubmitChat(
                MakeSemanticInput("user-a", "after pump")
            );
        Require(
            PumpSuccessor.Status == ETSPipelineAdmissionStatus::Accepted &&
                PumpSuccessor.AffectedEmissionId != A.AffectedEmissionId,
            "A pumped batch must not accept later accumulation"
        );

        FTSEventPipelineCoordinator ReadyCoordinator;
        const FTSPipelineAdmissionResult First = ReadyCoordinator.SubmitChat(
            MakeSemanticInput("same-user", "ready")
        );
        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                ReadyCoordinator.GetPendingChatBatchCount() == 0,
            "A directly ready Chat must never enter the mutable index"
        );
        const FTSPipelineAdmissionResult Successor = ReadyCoordinator.SubmitChat(
            MakeSemanticInput("same-user", "successor")
        );
        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                Successor.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.AffectedEmissionId != Successor.AffectedEmissionId &&
                ReadyCoordinator.GetPendingChatBatchCount() == 1,
            "A ready batch must be closed before a same-user successor"
        );
        Require(
            ReadyCoordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::Dispatched,
            "First same-user batch must enter Processing"
        );
        const FTSPipelineAdmissionResult AppendedSuccessor =
            ReadyCoordinator.SubmitChat(
                MakeSemanticInput("same-user", "successor append")
            );
        Require(
            AppendedSuccessor.Status ==
                ETSPipelineAdmissionStatus::Accumulated &&
                AppendedSuccessor.AffectedEmissionId ==
                    Successor.AffectedEmissionId &&
                ReadChatPayload(
                    ReadyCoordinator,
                    Successor.AffectedEmissionId
                ).Messages.size() == 2,
            "Messages while the predecessor is Processing append to the successor"
        );
        ReadyCoordinator.ValidateInternalConsistency();
    }

    void TestChatAdmissionAutoPumpClosesOlderMutableBatch()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSPipelineAdmissionResult Blocker = Coordinator.SubmitChat(
            MakeSemanticInput("blocker-user", "blocker")
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::Dispatched,
            "Blocker must enter Processing"
        );

        const FTSPipelineAdmissionResult Older = Coordinator.SubmitChat(
            MakeSemanticInput("older-user", "older")
        );
        Require(
            Older.Status == ETSPipelineAdmissionStatus::Accepted &&
                Coordinator.GetPendingChatBatchCount() == 1,
            "Older Chat must remain mutable while the blocker is Processing"
        );
        Require(
            Coordinator.CompleteChatProcessing(
                Blocker.AffectedEmissionId,
                ETSProcessingResult::Cancelled
            ).CancelResult.has_value(),
            "Cancelling the blocker must leave the older Chat pending"
        );

        const FTSPipelineAdmissionResult Newer = Coordinator.SubmitChat(
            MakeSemanticInput("newer-user", "newer")
        );
        Require(
            Newer.Status == ETSPipelineAdmissionStatus::Accepted &&
                Newer.EnqueueResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Newer.EnqueueResult->AutoPumpOutcome.ReadyEmission.EmissionId ==
                    Older.AffectedEmissionId &&
                Coordinator.GetPendingChatBatchCount() == 1,
            "Admission Auto Pump must close only the older selected Chat"
        );
        Require(
            Coordinator.SubmitChat(
                MakeSemanticInput("newer-user", "newer append")
            ).Status == ETSPipelineAdmissionStatus::Accumulated,
            "The newly admitted pending Chat must remain mutable"
        );
        Coordinator.ValidateInternalConsistency();
    }

    void TestChatLimitsRejectWithoutPartialMutation()
    {
        FTSEventPipelineSettings Settings;
        Settings.Chat.MaxMessagesPerBatch = 2;
        Settings.Chat.MaxMessageUtf8Bytes = 32;
        Settings.Chat.MaxBatchUtf8Bytes = 128;
        FTSEventPipelineCoordinator Coordinator(
            MakePendingCoreSettings(),
            {},
            Settings
        );
        FTSChatInput FirstInput = MakeSemanticInput("limited-user", "one");
        FirstInput.Emotes.clear();
        const FTSPipelineAdmissionResult First = Coordinator.SubmitChat(
            std::move(FirstInput)
        );
        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted,
            "First message"
        );
        FTSChatInput Second = MakeSemanticInput("limited-user", "two");
        Second.Emotes.clear();
        Require(
            Coordinator.SubmitChat(std::move(Second)).Status ==
                ETSPipelineAdmissionStatus::Accumulated,
            "Second message"
        );
        const FTSChatPayload Before = ReadChatPayload(
            Coordinator,
            First.AffectedEmissionId
        );

        FTSChatInput Third = MakeSemanticInput("limited-user", "three");
        Third.Emotes.clear();
        Third.User.Nickname = "must-not-replace";
        const FTSPipelineAdmissionResult Rejected = Coordinator.SubmitChat(Third);
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedSemanticLimit &&
                Rejected.AffectedEmissionId == 0 &&
                !Rejected.EnqueueResult.has_value(),
            "Message-count limit must reject before mutation"
        );
        const FTSChatPayload After = ReadChatPayload(
            Coordinator,
            First.AffectedEmissionId
        );
        Require(After.Messages.size() == Before.Messages.size(), "Atomic message count");
        Require(After.User.Nickname == Before.User.Nickname, "Atomic user snapshot");

        FTSChatInput Oversized = MakeSemanticInput("large-user", std::string(33, 'x'));
        Require(
            Coordinator.SubmitChat(std::move(Oversized)).Status ==
                ETSPipelineAdmissionStatus::RejectedSemanticLimit,
            "Individual message bytes must be limited"
        );
    }

    void TestChatEmoteAndBatchByteCostsAreEnforced()
    {
        FTSEventPipelineSettings Settings;
        Settings.Chat.MaxMessageUtf8Bytes = 10;
        Settings.Chat.MaxBatchUtf8Bytes = 64;
        FTSEventPipelineCoordinator Coordinator(
            MakePendingCoreSettings(),
            {},
            Settings
        );
        FTSChatInput Input = MakeSemanticInput("emote-user", "x");
        Input.Emotes = {FTSEmoteInfo{"12345", "67890"}};
        Require(
            Coordinator.SubmitChat(Input).Status ==
                ETSPipelineAdmissionStatus::RejectedSemanticLimit,
            "Emote identifiers and URLs must count toward message bytes"
        );

        Settings.Chat.MaxMessageUtf8Bytes = 32;
        Settings.Chat.MaxBatchUtf8Bytes = 32;
        FTSEventPipelineCoordinator BatchCoordinator(
            MakePendingCoreSettings(),
            {},
            Settings
        );
        FTSChatInput BatchInput = MakeSemanticInput("long-user-id", "short");
        BatchInput.User.Nickname = std::string(20, 'n');
        BatchInput.User.ProfilePictureUrl.clear();
        BatchInput.Emotes.clear();
        Require(
            BatchCoordinator.SubmitChat(std::move(BatchInput)).Status ==
                ETSPipelineAdmissionStatus::RejectedSemanticLimit,
            "User snapshot bytes must count toward the complete batch"
        );

        Settings.Chat.MaxMessageUtf8Bytes = 10;
        Settings.Chat.MaxBatchUtf8Bytes = 20;
        FTSEventPipelineCoordinator AppendCoordinator(
            MakePendingCoreSettings(),
            {},
            Settings
        );
        FTSChatInput First = MakeSemanticInput("u", std::string(8, 'a'));
        First.User.Nickname.clear();
        First.User.ProfilePictureUrl.clear();
        First.Emotes.clear();
        FTSChatInput Second = First;
        Second.Comment = std::string(8, 'b');
        FTSChatInput Third = First;
        Third.Comment = std::string(8, 'c');
        Require(
            AppendCoordinator.SubmitChat(First).Status ==
                ETSPipelineAdmissionStatus::Accepted &&
                AppendCoordinator.SubmitChat(Second).Status ==
                    ETSPipelineAdmissionStatus::Accumulated &&
                AppendCoordinator.SubmitChat(Third).Status ==
                    ETSPipelineAdmissionStatus::RejectedSemanticLimit,
            "Complete batch byte limit must apply before append commit"
        );
    }

    void TestChatAccumulationWorksAtCapacityAndSuccessorDoesNotLeak()
    {
        FTSEventPipelineCoordinator Pending(
            MakePendingCoreSettings(30s, 1)
        );
        const FTSPipelineAdmissionResult First = Pending.SubmitChat(
            MakeSemanticInput("capacity-user", "one")
        );
        Require(
            Pending.SubmitChat(
                MakeSemanticInput("capacity-user", "two")
            ).Status == ETSPipelineAdmissionStatus::Accumulated,
            "Accumulation must not consume another Core slot"
        );
        Require(
            First.AffectedEmissionId != 0 && Pending.GetBindingCount() == 1,
            "Capacity accumulation must preserve one binding"
        );

        FTSEventQueueSettings ProcessingSettings =
            MakeOperationalChatSettings(
                true,
                false,
                30s,
                ETSEventExpirePolicy::Discard
            );
        FTSFlowQueueSettings* ProcessingChat =
            ProcessingSettings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ProcessingChat != nullptr, "Processing Chat settings");
        ProcessingChat->MaxSlots = 1;
        FTSEventPipelineCoordinator Processing(ProcessingSettings);
        const FTSPipelineAdmissionResult Active = Processing.SubmitChat(
            MakeSemanticInput("successor-user", "active")
        );
        Require(Active.Status == ETSPipelineAdmissionStatus::Accepted, "Active admission");
        Require(
            Processing.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::Dispatched,
            "Active batch must enter Processing"
        );
        const FTSPipelineAdmissionResult Rejected = Processing.SubmitChat(
            MakeSemanticInput("successor-user", "rejected successor")
        );
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Processing.GetBindingCount() == 1 &&
                Processing.GetChatPayloadCount() == 1 &&
                Processing.GetPendingChatBatchCount() == 0,
            "Rejected successor must leave no provisional state"
        );
        Processing.ValidateInternalConsistency();
    }

    void TestChatAppendDoesNotRenewTtlAndBoundaryCreatesSuccessor()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakePendingCoreSettings(5s),
            Clock.MakeProvider()
        );
        const FTSPipelineAdmissionResult First = Coordinator.SubmitChat(
            MakeSemanticInput("ttl-user", "one")
        );
        const FTSEventQueueTimePoint FirstExpiry =
            First.EnqueueResult->AdmittedEmission.ExpiresAt;
        Clock.Advance(4s);
        Require(
            Coordinator.SubmitChat(
                MakeSemanticInput("ttl-user", "two")
            ).Status == ETSPipelineAdmissionStatus::Accumulated,
            "Append before TTL must accumulate"
        );
        Require(
            Coordinator.GetNextWakeTime().WakeTime == FirstExpiry,
            "Append must not renew ExpiresAt"
        );
        const FTSChatPayload AccumulatedPayload = ReadChatPayload(
            Coordinator,
            First.AffectedEmissionId
        );
        Require(
            AccumulatedPayload.Messages.size() == 2 &&
                AccumulatedPayload.Messages[0].ReceivedAt ==
                    FTSEventQueueTimePoint{} &&
                AccumulatedPayload.Messages[1].ReceivedAt == Clock.Now,
            "Each accumulated message must retain its own Pipeline timestamp"
        );

        Clock.Advance(1s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents[0].Envelope.EmissionId ==
                    First.AffectedEmissionId &&
                Expirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard &&
                Coordinator.GetPendingChatBatchCount() == 0,
            "Boundary maintenance must terminalize the expired predecessor"
        );
        const FTSPipelineAdmissionResult Successor = Coordinator.SubmitChat(
            MakeSemanticInput("ttl-user", "at boundary")
        );
        Require(
            Successor.Status == ETSPipelineAdmissionStatus::Accepted &&
                Successor.AffectedEmissionId != First.AffectedEmissionId &&
                Successor.EnqueueResult->LifecycleEvents.empty() &&
                Coordinator.GetPendingChatBatchCount() == 1,
            "Admission after boundary maintenance must create the successor"
        );
        Require(
            !Coordinator.VisitEmissionBinding(
                First.AffectedEmissionId,
                [](const FTSEmissionBinding&) {}
            ),
            "Expired predecessor binding must be cleaned"
        );
        Coordinator.ValidateInternalConsistency();
    }

    void TestPayloadRepositoryPreparedReplacementIsAtomic()
    {
        FTSChatPayloadRepository Repository;
        FTSChatPayload Original = MakeChatPayloadFromInput(
            MakeSemanticInput("repository-user", "old")
        );
        const std::optional<FTSPayloadHandle> Handle = Repository.Insert(Original);
        Require(Handle.has_value(), "Replacement setup handle");

        FTSChatPayload Replacement = MakeChatPayloadFromInput(
            MakeSemanticInput("repository-user", "new")
        );
        auto Prepared = Repository.PrepareReplace(*Handle, Replacement);
        Require(Prepared.has_value(), "Known handle must prepare replacement");
        Require(
            Repository.Visit(
                *Handle,
                [](const FTSChatPayload& Payload)
                {
                    Require(
                        Payload.Messages[0].Comment == "old",
                        "Pre-commit payload"
                    );
                }
            ),
            "Known payload must be visitable before replacement"
        );
        Repository.CommitReplace(*Prepared);
        Require(
            Repository.Visit(
                *Handle,
                [](const FTSChatPayload& Payload)
                {
                    Require(
                        Payload.Messages[0].Comment == "new",
                        "Committed payload"
                    );
                }
            ),
            "Known payload must be visitable after replacement"
        );
        Require(
            !Repository.PrepareReplace(FTSPayloadHandle{999}, Replacement).has_value(),
            "Unknown handle must reject replacement"
        );
    }

    void TestChatPendingIndexPreparedOperations()
    {
        FTSChatPendingBatchIndex Index;
        auto Insert = Index.PrepareInsert(
            "indexed-user",
            FTSChatPendingBatchEntry{1, FTSEventQueueTimePoint{}}
        );
        Require(Insert.has_value() && Index.Size() == 0, "Prepared insert invisible");
        Index.CommitInsert(*Insert);
        Require(Index.ContainsExact("indexed-user", 1), "Committed exact insert");
        Require(
            !Index.PrepareInsert(
                "indexed-user",
                FTSChatPendingBatchEntry{2, {}}
            ).has_value(),
            "Duplicate user key must be rejected"
        );

        auto Replace = Index.PrepareReplaceExact(
            "indexed-user",
            1,
            FTSChatPendingBatchEntry{2, FTSEventQueueTimePoint{}}
        );
        Require(Replace.has_value(), "Exact replacement must prepare");
        Index.CommitReplaceExact(*Replace);
        Require(Index.ContainsExact("indexed-user", 2), "Exact replacement commit");

        auto Erase = Index.PrepareEraseExact("indexed-user", 2);
        Require(Erase.has_value(), "Exact erase must prepare");
        Index.CommitEraseExact(*Erase);
        Require(Index.Size() == 0, "Exact erase must remove one association");
    }
}

namespace TikStudio::Tests
{
    void RegisterChatSemanticTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Chat semantic settings defaults and valid custom settings", &TestChatSemanticSettingsDefaultsAndValidCustomSettings});
        Tests.push_back({"Chat semantic settings reject invalid prefixes", &TestChatSemanticSettingsRejectInvalidPrefixes});
        Tests.push_back({"Chat semantic settings reject zero limits and construction", &TestChatSemanticSettingsRejectZeroLimitsAndConstruction});
        Tests.push_back({"Chat command classification rules", &TestChatCommandClassificationRules});
        Tests.push_back({"Chat command modes and filtered input preserve batch", &TestChatCommandModesAndFilteredInputDoNotMutateBatch});
        Tests.push_back({"Chat initial payload timestamp command and priority", &TestChatInitialPayloadTimestampCommandAndPriority});
        Tests.push_back({"Chat accumulation identity order and frozen priority", &TestChatAccumulationPreservesIdentityOrderAndFrozenPriority});
        Tests.push_back({"Chat different users and ready successor remain separate", &TestChatDifferentUsersAndReadySuccessorRemainSeparate});
        Tests.push_back({"Chat admission Auto Pump closes older mutable batch", &TestChatAdmissionAutoPumpClosesOlderMutableBatch});
        Tests.push_back({"Chat limits reject without partial mutation", &TestChatLimitsRejectWithoutPartialMutation});
        Tests.push_back({"Chat emote and batch byte costs are enforced", &TestChatEmoteAndBatchByteCostsAreEnforced});
        Tests.push_back({"Chat accumulation at capacity and rejected successor cleanup", &TestChatAccumulationWorksAtCapacityAndSuccessorDoesNotLeak});
        Tests.push_back({"Chat append keeps TTL and boundary creates successor", &TestChatAppendDoesNotRenewTtlAndBoundaryCreatesSuccessor});
        Tests.push_back({"Payload repository prepared replacement is atomic", &TestPayloadRepositoryPreparedReplacementIsAtomic});
        Tests.push_back({"Chat pending index prepared operations", &TestChatPendingIndexPreparedOperations});
    }
}
