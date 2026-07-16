#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"
#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<FTSChatPayloadRepository>);
static_assert(!std::is_copy_assignable_v<FTSChatPayloadRepository>);
static_assert(!std::is_move_constructible_v<FTSChatPayloadRepository>);
static_assert(!std::is_move_assignable_v<FTSChatPayloadRepository>);

static_assert(!std::is_copy_constructible_v<FTSEmissionBindingRegistry>);
static_assert(!std::is_copy_assignable_v<FTSEmissionBindingRegistry>);
static_assert(!std::is_move_constructible_v<FTSEmissionBindingRegistry>);
static_assert(!std::is_move_assignable_v<FTSEmissionBindingRegistry>);

static_assert(!std::is_copy_constructible_v<FTSEventPipelineCoordinator>);
static_assert(!std::is_copy_assignable_v<FTSEventPipelineCoordinator>);
static_assert(!std::is_move_constructible_v<FTSEventPipelineCoordinator>);
static_assert(!std::is_move_assignable_v<FTSEventPipelineCoordinator>);

namespace
{
    using namespace std::chrono_literals;

    struct FControlledClock
    {
        FTSEventQueueTimePoint Now{};

        [[nodiscard]]
        FTSNowProvider MakeProvider()
        {
            return [this]()
            {
                return Now;
            };
        }

        template <typename Rep, typename Period>
        void Advance(std::chrono::duration<Rep, Period> Delta)
        {
            Now += std::chrono::duration_cast<FTSEventQueueClock::duration>(
                Delta
            );
        }
    };

    void Require(bool bCondition, const std::string& Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    void RequireUserEqual(
        const FTSUserSnapshot& Actual,
        const FTSUserSnapshot& Expected,
        const std::string& Context
    )
    {
        Require(Actual.UniqueId == Expected.UniqueId, Context + ": UniqueId");
        Require(Actual.Nickname == Expected.Nickname, Context + ": Nickname");
        Require(
            Actual.ProfilePictureUrl == Expected.ProfilePictureUrl,
            Context + ": ProfilePictureUrl"
        );
        Require(Actual.FollowRole == Expected.FollowRole, Context + ": FollowRole");
        Require(
            Actual.bIsModerator == Expected.bIsModerator,
            Context + ": bIsModerator"
        );
        Require(
            Actual.bIsSubscriber == Expected.bIsSubscriber,
            Context + ": bIsSubscriber"
        );
        Require(
            Actual.bIsNewGifter == Expected.bIsNewGifter,
            Context + ": bIsNewGifter"
        );
        Require(
            Actual.TopGifterRank == Expected.TopGifterRank,
            Context + ": TopGifterRank"
        );
        Require(
            Actual.GifterLevel == Expected.GifterLevel,
            Context + ": GifterLevel"
        );
        Require(
            Actual.TeamMemberLevel == Expected.TeamMemberLevel,
            Context + ": TeamMemberLevel"
        );
    }

    void RequireChatInputEqual(
        const FTSChatInput& Actual,
        const FTSChatInput& Expected,
        const std::string& Context
    )
    {
        Require(Actual.Comment == Expected.Comment, Context + ": Comment");
        Require(
            Actual.Emotes.size() == Expected.Emotes.size(),
            Context + ": Emote count"
        );

        for (std::size_t Index = 0; Index < Expected.Emotes.size(); ++Index)
        {
            Require(
                Actual.Emotes[Index].EmoteId == Expected.Emotes[Index].EmoteId,
                Context + ": EmoteId"
            );
            Require(
                Actual.Emotes[Index].EmoteImageUrl
                    == Expected.Emotes[Index].EmoteImageUrl,
                Context + ": EmoteImageUrl"
            );
        }

        RequireUserEqual(Actual.User, Expected.User, Context + ": User");
    }

    [[nodiscard]]
    FTSChatInput MakeCompleteInput()
    {
        FTSChatInput Input;
        Input.Comment = "Portable chat snapshot";
        Input.Emotes = {
            FTSEmoteInfo{"emote-wave", "https://example.test/wave.png"},
            FTSEmoteInfo{"emote-heart", "https://example.test/heart.png"}
        };
        Input.User.UniqueId = "user-42";
        Input.User.Nickname = "Chat User";
        Input.User.ProfilePictureUrl = "https://example.test/user.png";
        Input.User.FollowRole = 3;
        Input.User.bIsModerator = true;
        Input.User.bIsSubscriber = true;
        Input.User.bIsNewGifter = true;
        Input.User.TopGifterRank = 7;
        Input.User.GifterLevel = 11;
        Input.User.TeamMemberLevel = 13;
        return Input;
    }

    [[nodiscard]]
    FTSEventQueueSettings MakeChatSettings(
        bool bEnabled,
        std::uint32_t MaxSlots
    )
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Chat settings must be available");
        ChatSettings->bEnabled = bEnabled;
        ChatSettings->MaxSlots = MaxSlots;
        return Settings;
    }

    [[nodiscard]]
    FTSEventQueueSettings MakeOperationalChatSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm,
        std::chrono::milliseconds TTL = 8s,
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSEventQueueSettings Settings = MakeChatSettings(true, 10);
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Chat settings must be available");
        ChatSettings->TTL = TTL;
        ChatSettings->ExpirePolicy = ExpirePolicy;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    [[nodiscard]]
    FTSEmissionId SubmitAcceptedChat(
        FTSEventPipelineCoordinator& Coordinator,
        const std::string& Comment
    )
    {
        FTSChatInput Input = MakeCompleteInput();
        Input.Comment = Comment;
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(std::move(Input));

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Chat admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AdmittedEmission.EmissionId != 0,
            "Accepted Chat admission must have a valid identity"
        );
        return Admission.EnqueueResult->AdmittedEmission.EmissionId;
    }

    [[nodiscard]]
    FTSEmissionId BeginReadyChat(
        FTSEventPipelineCoordinator& Coordinator
    )
    {
        const FTSChatDispatchResult Dispatch =
            Coordinator.BeginChatProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "A ready Chat must produce a dispatch"
        );
        return Dispatch.Dispatch->Emission.EmissionId;
    }

    void TestChatCandidateAndAdmissionDefaults()
    {
        FTSChatInput Input;
        Input.Comment = "Hello";

        const TTSFamilyDecision<FTSChatPayload> Decision =
            FTSChatFamily::Decide(Input);

        Require(Decision.has_value(), "Chat must produce an admission candidate");

        const TTSAdmissionCandidate<FTSChatPayload>& Candidate = *Decision;
        Require(
            Candidate.FamilyKind == ETSEventFamilyKind::Chat,
            "Chat candidate FamilyKind mismatch"
        );
        Require(
            Candidate.EnqueueRequest.Flow == ETSEventFlow::Chat,
            "Chat candidate Flow mismatch"
        );
        Require(
            Candidate.EnqueueRequest.PriorityAdjustment == 0,
            "Chat candidate must not adjust priority"
        );
        Require(
            !Candidate.EnqueueRequest.bOverrideTTL,
            "Chat candidate must not override TTL"
        );
        Require(
            Candidate.EnqueueRequest.TTLOverride.count() == 0,
            "Chat candidate TTLOverride default mismatch"
        );
        Require(
            !Candidate.EnqueueRequest.bProtectedFromEviction,
            "Chat candidate must not request special eviction protection"
        );
    }

    void TestChatPayloadSnapshotAndInputPreservation()
    {
        FTSChatInput Input = MakeCompleteInput();
        const FTSChatInput Original = Input;

        const TTSFamilyDecision<FTSChatPayload> Decision =
            FTSChatFamily::Decide(Input);

        Require(Decision.has_value(), "Chat snapshot must produce a candidate");
        RequireChatInputEqual(
            Decision->Payload.Input,
            Original,
            "Chat payload snapshot"
        );
        RequireChatInputEqual(
            Input,
            Original,
            "Chat input after by-copy decision"
        );
    }

    void TestTypedRepositoryStoresIndependentSnapshots()
    {
        FTSChatPayloadRepository Repository;
        Require(Repository.Empty(), "Repository must start empty");
        Require(Repository.Size() == 0, "Empty repository size must be zero");

        FTSChatPayload FirstPayload;
        FirstPayload.Input = MakeCompleteInput();
        const FTSChatInput ExpectedFirstInput = FirstPayload.Input;

        const std::optional<FTSPayloadHandle> FirstHandle =
            Repository.Insert(FirstPayload);
        Require(FirstHandle.has_value(), "First payload insertion failed");
        Require(FirstHandle->Value != 0, "First payload handle must be non-zero");

        FTSChatPayload SecondPayload;
        SecondPayload.Input.Comment = "Second payload";
        const FTSChatInput ExpectedSecondInput = SecondPayload.Input;

        const std::optional<FTSPayloadHandle> SecondHandle =
            Repository.Insert(SecondPayload);
        Require(SecondHandle.has_value(), "Second payload insertion failed");
        Require(SecondHandle->Value != 0, "Second payload handle must be non-zero");
        Require(
            FirstHandle->Value != SecondHandle->Value,
            "Two payloads must receive distinct handles"
        );
        Require(!Repository.Empty(), "Repository with payloads must not be empty");
        Require(Repository.Size() == 2, "Repository size must reflect both payloads");

        FirstPayload.Input.Comment = "Mutated outside repository";
        FirstPayload.Input.Emotes.clear();
        FirstPayload.Input.User.Nickname = "Mutated User";

        const bool bVisitedFirst = Repository.Visit(
            *FirstHandle,
            [&](const FTSChatPayload& StoredPayload)
            {
                RequireChatInputEqual(
                    StoredPayload.Input,
                    ExpectedFirstInput,
                    "Stored first payload"
                );
            }
        );
        Require(bVisitedFirst, "Visit must find the first payload");

        const bool bVisitedSecond = Repository.Visit(
            *SecondHandle,
            [&](const FTSChatPayload& StoredPayload)
            {
                RequireChatInputEqual(
                    StoredPayload.Input,
                    ExpectedSecondInput,
                    "Stored second payload"
                );
            }
        );
        Require(bVisitedSecond, "Visit must find the second payload");
    }

    void TestTypedRepositoryEraseAndHandleInvariants()
    {
        FTSChatPayloadRepository Repository;
        bool bCallbackCalled = false;

        const bool bFoundZero = Repository.Visit(
            FTSPayloadHandle{},
            [&](const FTSChatPayload&)
            {
                bCallbackCalled = true;
            }
        );
        Require(!bFoundZero, "Zero handle must not find a payload");
        Require(!bCallbackCalled, "Zero handle must not invoke the callback");

        const FTSPayloadHandle UnknownHandle{9999};
        const bool bFoundUnknown = Repository.Visit(
            UnknownHandle,
            [&](const FTSChatPayload&)
            {
                bCallbackCalled = true;
            }
        );
        Require(!bFoundUnknown, "Unknown handle must not find a payload");
        Require(!bCallbackCalled, "Unknown handle must not invoke the callback");
        Require(!Repository.Erase(FTSPayloadHandle{}), "Zero handle must not erase");
        Require(!Repository.Erase(UnknownHandle), "Unknown handle must not erase");

        FTSChatPayload FirstPayload;
        FirstPayload.Input.Comment = "First erasable payload";
        FTSChatPayload SecondPayload;
        SecondPayload.Input.Comment = "Second retained payload";

        const std::optional<FTSPayloadHandle> FirstHandle =
            Repository.Insert(FirstPayload);
        const std::optional<FTSPayloadHandle> SecondHandle =
            Repository.Insert(SecondPayload);
        Require(
            FirstHandle.has_value() && SecondHandle.has_value(),
            "Repository setup insertions failed"
        );

        Require(Repository.Erase(*FirstHandle), "First erase must succeed");
        Require(!Repository.Erase(*FirstHandle), "Second erase must fail");
        Require(Repository.Size() == 1, "Erase must remove exactly one payload");
        Require(
            !Repository.Visit(*FirstHandle, [](const FTSChatPayload&) {}),
            "Erased handle must no longer find a payload"
        );

        FTSChatPayload ThirdPayload;
        ThirdPayload.Input.Comment = "Third payload";
        const std::optional<FTSPayloadHandle> ThirdHandle =
            Repository.Insert(ThirdPayload);
        Require(ThirdHandle.has_value(), "Third payload insertion failed");
        Require(
            ThirdHandle->Value != FirstHandle->Value,
            "An erased handle must not be reused"
        );
        Require(
            ThirdHandle->Value != SecondHandle->Value,
            "A live handle must remain unique"
        );

        Require(Repository.Erase(*SecondHandle), "Second payload erase failed");
        Require(Repository.Erase(*ThirdHandle), "Third payload erase failed");
        Require(Repository.Empty(), "Repository must be empty after all erases");
        Require(Repository.Size() == 0, "Empty repository size must return to zero");
    }

    [[nodiscard]]
    FTSEmissionBinding MakeBinding(
        FTSEmissionId EmissionId,
        FTSPayloadHandle PayloadHandle,
        ETSEventFlow ExpectedFlow = ETSEventFlow::Chat,
        ETSExternalEmissionState ExternalState = ETSExternalEmissionState::Bound
    )
    {
        FTSEmissionBinding Binding;
        Binding.EmissionId = EmissionId;
        Binding.FamilyKind = ETSEventFamilyKind::Chat;
        Binding.ExpectedFlow = ExpectedFlow;
        Binding.PayloadHandle = PayloadHandle;
        Binding.ExternalState = ExternalState;
        return Binding;
    }

    void TestBindingRegistryInsertVisitAndDuplicateProtection()
    {
        FTSEmissionBindingRegistry Registry;
        Require(Registry.Empty(), "A new binding registry must be empty");
        Require(Registry.Size() == 0, "A new binding registry size must be zero");

        const FTSEmissionBinding Original = MakeBinding(
            101,
            FTSPayloadHandle{11}
        );
        Require(Registry.Insert(Original), "Valid binding insertion failed");
        Require(!Registry.Empty(), "Inserted binding must make the registry non-empty");
        Require(Registry.Size() == 1, "Registry must contain the inserted binding");

        bool bVisited = false;
        Require(
            Registry.Visit(
                101,
                [&](const FTSEmissionBinding& Stored)
                {
                    bVisited = true;
                    Require(
                        Stored.EmissionId == Original.EmissionId,
                        "Stored EmissionId mismatch"
                    );
                    Require(
                        Stored.FamilyKind == Original.FamilyKind,
                        "Stored FamilyKind mismatch"
                    );
                    Require(
                        Stored.ExpectedFlow == Original.ExpectedFlow,
                        "Stored ExpectedFlow mismatch"
                    );
                    Require(
                        Stored.PayloadHandle.Value == Original.PayloadHandle.Value,
                        "Stored PayloadHandle mismatch"
                    );
                    Require(
                        Stored.ExternalState == Original.ExternalState,
                        "Stored ExternalState mismatch"
                    );
                }
            ),
            "Visit must find an inserted binding"
        );
        Require(bVisited, "Visit must invoke the callback for an inserted binding");

        FTSEmissionBinding Duplicate = MakeBinding(
            101,
            FTSPayloadHandle{99},
            ETSEventFlow::Gift,
            ETSExternalEmissionState::Processing
        );
        Duplicate.FamilyKind = ETSEventFamilyKind::Gift;
        Require(!Registry.Insert(Duplicate), "Duplicate EmissionId must be rejected");
        Require(Registry.Size() == 1, "Rejected duplicate must not change registry size");

        Require(
            Registry.Visit(
                101,
                [](const FTSEmissionBinding& Stored)
                {
                    Require(
                        Stored.FamilyKind == ETSEventFamilyKind::Chat,
                        "Duplicate must not replace the original family"
                    );
                    Require(
                        Stored.ExpectedFlow == ETSEventFlow::Chat,
                        "Duplicate must not replace the original flow"
                    );
                    Require(
                        Stored.PayloadHandle.Value == 11,
                        "Duplicate must not replace the original payload handle"
                    );
                    Require(
                        Stored.ExternalState == ETSExternalEmissionState::Bound,
                        "Duplicate must not replace the original external state"
                    );
                }
            ),
            "Original binding must remain queryable after duplicate rejection"
        );
    }

    void TestBindingRegistryRejectsInvalidBindings()
    {
        FTSEmissionBindingRegistry Registry;

        Require(
            !Registry.Insert(MakeBinding(0, FTSPayloadHandle{1})),
            "EmissionId zero must be rejected"
        );
        Require(
            !Registry.Insert(MakeBinding(1, FTSPayloadHandle{})),
            "PayloadHandle zero must be rejected"
        );
        Require(
            !Registry.Insert(MakeBinding(
                2,
                FTSPayloadHandle{2},
                ETSEventFlow::Count
            )),
            "Invalid ExpectedFlow must be rejected"
        );
        Require(Registry.Empty(), "Invalid bindings must not change the registry");
        Require(Registry.Size() == 0, "Invalid bindings must keep registry size at zero");

        bool bCallbackCalled = false;
        Require(
            !Registry.Visit(
                0,
                [&](const FTSEmissionBinding&)
                {
                    bCallbackCalled = true;
                }
            ),
            "EmissionId zero must not resolve a binding"
        );
        Require(
            !Registry.Visit(
                999,
                [&](const FTSEmissionBinding&)
                {
                    bCallbackCalled = true;
                }
            ),
            "Unknown EmissionId must not resolve a binding"
        );
        Require(!bCallbackCalled, "Missing bindings must not invoke Visit callbacks");
    }

    void TestBindingRegistryConditionalTransitions()
    {
        FTSEmissionBindingRegistry Registry;
        Require(
            Registry.Insert(MakeBinding(201, FTSPayloadHandle{21})),
            "First transition binding insertion failed"
        );
        Require(
            Registry.Insert(MakeBinding(202, FTSPayloadHandle{22})),
            "Second transition binding insertion failed"
        );

        Require(
            !Registry.TransitionState(
                201,
                ETSExternalEmissionState::Processing,
                ETSExternalEmissionState::TerminalPendingHandling
            ),
            "Transition with the wrong expected state must fail"
        );
        Require(
            Registry.TransitionState(
                201,
                ETSExternalEmissionState::Bound,
                ETSExternalEmissionState::Processing
            ),
            "Bound must transition to Processing"
        );
        Require(
            Registry.TransitionState(
                201,
                ETSExternalEmissionState::Processing,
                ETSExternalEmissionState::TerminalPendingHandling
            ),
            "Processing must transition to TerminalPendingHandling"
        );
        Require(
            Registry.TransitionState(
                202,
                ETSExternalEmissionState::Bound,
                ETSExternalEmissionState::TerminalPendingHandling
            ),
            "Bound must transition directly to TerminalPendingHandling"
        );
        Require(
            !Registry.TransitionState(
                999,
                ETSExternalEmissionState::Bound,
                ETSExternalEmissionState::Processing
            ),
            "Unknown EmissionId must not transition"
        );

        Require(
            Registry.Visit(
                201,
                [](const FTSEmissionBinding& Stored)
                {
                    Require(
                        Stored.ExternalState
                            == ETSExternalEmissionState::TerminalPendingHandling,
                        "Processing path must end in TerminalPendingHandling"
                    );
                }
            ),
            "First transitioned binding must remain queryable"
        );
        Require(
            Registry.Visit(
                202,
                [](const FTSEmissionBinding& Stored)
                {
                    Require(
                        Stored.ExternalState
                            == ETSExternalEmissionState::TerminalPendingHandling,
                        "Direct Bound path must end in TerminalPendingHandling"
                    );
                }
            ),
            "Second transitioned binding must remain queryable"
        );
    }

    void TestBindingRegistryEraseAndSize()
    {
        FTSEmissionBindingRegistry Registry;
        Require(
            Registry.Insert(MakeBinding(301, FTSPayloadHandle{31})),
            "First erase binding insertion failed"
        );
        Require(
            Registry.Insert(MakeBinding(302, FTSPayloadHandle{32})),
            "Second erase binding insertion failed"
        );
        Require(Registry.Size() == 2, "Registry size must reflect both bindings");

        Require(!Registry.Erase(0), "EmissionId zero must not erase a binding");
        Require(!Registry.Erase(999), "Unknown EmissionId must not erase a binding");
        Require(Registry.Erase(301), "Existing binding must erase once");
        Require(!Registry.Erase(301), "Erased binding must not erase twice");
        Require(Registry.Size() == 1, "Erase must remove exactly one binding");
        Require(
            !Registry.Visit(301, [](const FTSEmissionBinding&) {}),
            "Erased binding must no longer be queryable"
        );

        Require(Registry.Erase(302), "Remaining binding erase failed");
        Require(Registry.Empty(), "Registry must be empty after all erases");
        Require(Registry.Size() == 0, "Empty registry size must return to zero");
    }

    void TestChatCoordinatorFirstAdmissionAutoPumps()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();

        const FTSPipelineAdmissionResult Result = Coordinator.SubmitChat(Input);
        Require(
            Result.Status == ETSPipelineAdmissionStatus::Accepted,
            "First Chat admission must be accepted by the coordinator"
        );
        Require(Result.EnqueueResult.has_value(), "Accepted admission must expose core result");

        const FTSEnqueueResult& CoreResult = *Result.EnqueueResult;
        Require(
            CoreResult.Status == ETSEnqueueStatus::Accepted,
            "First Chat admission must be accepted by the core"
        );
        Require(
            CoreResult.AutoPumpOutcome.Status == ETSPumpStatus::EmissionReady,
            "First Chat admission must Auto Pump while idle"
        );
        Require(
            CoreResult.AutoPumpOutcome.ReadyEmission.EmissionId
                == CoreResult.AdmittedEmission.EmissionId,
            "Auto Pump ready identity must match the admitted identity"
        );

        bool bVisitedBinding = false;
        Require(
            Coordinator.VisitEmissionBinding(
                CoreResult.AdmittedEmission.EmissionId,
                [&](const FTSEmissionBinding& Binding)
                {
                    bVisitedBinding = true;
                    Require(
                        Binding.FamilyKind == ETSEventFamilyKind::Chat,
                        "Accepted binding must belong to Chat"
                    );
                    Require(
                        Binding.ExpectedFlow == ETSEventFlow::Chat,
                        "Accepted binding must expect the Chat flow"
                    );
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Accepted binding must start Bound"
                    );
                    Require(
                        Binding.PayloadHandle.Value != 0,
                        "Accepted binding must reference a valid payload handle"
                    );
                }
            ),
            "Accepted Chat binding must be visitable"
        );
        Require(bVisitedBinding, "Binding callback must run for an accepted Chat");

        bool bVisitedPayload = false;
        Require(
            Coordinator.VisitChatPayloadForEmission(
                CoreResult.AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    bVisitedPayload = true;
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "First coordinated Chat payload"
                    );
                }
            ),
            "Accepted Chat payload must be resolvable by EmissionId"
        );
        Require(bVisitedPayload, "Payload callback must run for an accepted Chat");
        Require(Coordinator.GetBindingCount() == 1, "First admission must create one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "First admission must retain one Chat payload"
        );
    }

    void TestChatCoordinatorSecondAdmissionWhileBusy()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "First coordinated Chat";
        FTSChatInput SecondInput = MakeCompleteInput();
        SecondInput.Comment = "Second coordinated Chat";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitChat(SecondInput);

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First busy-path setup admission must succeed"
        );
        Require(
            Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Second Chat must still be admitted while the core is busy"
        );
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "First Chat must become InFlight"
        );
        Require(
            Second.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::NotRequested,
            "Second busy admission must report Auto Pump NotRequested"
        );
        Require(
            First.EnqueueResult->AdmittedEmission.EmissionId
                != Second.EnqueueResult->AdmittedEmission.EmissionId,
            "Two coordinated admissions must receive distinct identities"
        );
        Require(Coordinator.GetBindingCount() == 2, "Busy admission must retain two bindings");
        Require(
            Coordinator.GetChatPayloadCount() == 2,
            "Busy admission must retain two Chat payloads"
        );

        Require(
            Coordinator.VisitChatPayloadForEmission(
                First.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        FirstInput,
                        "First busy-path payload"
                    );
                }
            ),
            "First busy-path payload must remain resolvable"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                Second.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        SecondInput,
                        "Second busy-path payload"
                    );
                }
            ),
            "Second busy-path payload must be resolvable"
        );
    }

    void TestChatCoordinatorRejectsDisabledFlow()
    {
        FTSEventPipelineCoordinator Coordinator(MakeChatSettings(false, 30));

        const FTSPipelineAdmissionResult Result =
            Coordinator.SubmitChat(MakeCompleteInput());

        Require(
            Result.Status == ETSPipelineAdmissionStatus::RejectedByCore,
            "Disabled Chat must be classified as rejected by core"
        );
        Require(Result.EnqueueResult.has_value(), "Core rejection must retain its result");
        Require(
            Result.EnqueueResult->Status == ETSEnqueueStatus::RejectedDisabled,
            "Disabled Chat must preserve RejectedDisabled"
        );
        Require(Coordinator.GetBindingCount() == 0, "Disabled Chat must not create a binding");
        Require(
            Coordinator.GetChatPayloadCount() == 0,
            "Disabled Chat must roll back its provisional payload"
        );
    }

    void TestChatCoordinatorCapacityRejectionPreservesPriorAdmission()
    {
        FTSEventPipelineCoordinator Coordinator(MakeChatSettings(true, 1));
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "Capacity owner";
        FTSChatInput RejectedInput = MakeCompleteInput();
        RejectedInput.Comment = "Rejected at capacity";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First capacity admission must succeed"
        );

        const FTSEmissionId FirstEmissionId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitChat(RejectedInput);

        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore,
            "Second capacity admission must be rejected by core"
        );
        Require(Rejected.EnqueueResult.has_value(), "Capacity rejection must retain core result");
        Require(
            Rejected.EnqueueResult->Status == ETSEnqueueStatus::RejectedAtCapacity,
            "Second capacity admission must preserve RejectedAtCapacity"
        );
        Require(Coordinator.GetBindingCount() == 1, "Capacity rejection must keep one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "Capacity rejection must remove only its provisional payload"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstEmissionId,
                [](const FTSEmissionBinding&) {}
            ),
            "Capacity rejection must preserve the first binding"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                FirstEmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        FirstInput,
                        "Capacity-preserved payload"
                    );
                }
            ),
            "Capacity rejection must preserve the first payload"
        );
    }

    void TestChatCoordinatorUnknownEmissionInspection()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSPipelineAdmissionResult Accepted =
            Coordinator.SubmitChat(MakeCompleteInput());
        Require(
            Accepted.Status == ETSPipelineAdmissionStatus::Accepted,
            "Unknown inspection setup admission must succeed"
        );

        const std::size_t BindingCount = Coordinator.GetBindingCount();
        const std::size_t PayloadCount = Coordinator.GetChatPayloadCount();
        bool bBindingCallbackCalled = false;
        bool bPayloadCallbackCalled = false;

        Require(
            !Coordinator.VisitEmissionBinding(
                9999,
                [&](const FTSEmissionBinding&)
                {
                    bBindingCallbackCalled = true;
                }
            ),
            "Unknown binding inspection must return false"
        );
        Require(
            !Coordinator.VisitChatPayloadForEmission(
                9999,
                [&](const FTSChatPayload&)
                {
                    bPayloadCallbackCalled = true;
                }
            ),
            "Unknown payload inspection must return false"
        );
        Require(!bBindingCallbackCalled, "Unknown binding must not invoke its callback");
        Require(!bPayloadCallbackCalled, "Unknown payload must not invoke its callback");
        Require(
            Coordinator.GetBindingCount() == BindingCount,
            "Unknown inspection must not change binding count"
        );
        Require(
            Coordinator.GetChatPayloadCount() == PayloadCount,
            "Unknown inspection must not change payload count"
        );
    }

    void TestChatDispatchWithoutPendingReady()
    {
        FTSEventPipelineCoordinator Coordinator;

        const FTSChatDispatchResult Result =
            Coordinator.BeginChatProcessing();

        Require(
            Result.Status == ETSPipelineDispatchStatus::NoEmissionReady,
            "A new coordinator must not have an authorized ready"
        );
        Require(!Result.Dispatch.has_value(), "No ready must not produce a dispatch");
        Require(Coordinator.GetBindingCount() == 0, "No ready must not create bindings");
        Require(
            Coordinator.GetChatPayloadCount() == 0,
            "No ready must not create Chat payloads"
        );
    }

    void TestAuthorizedChatReadyProducesOwnedDispatch()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);

        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Authorized dispatch setup admission must succeed"
        );
        Require(
            Admission.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "Authorized dispatch must originate from a core EmissionReady"
        );

        const FTSChatDispatchResult Result =
            Coordinator.BeginChatProcessing();
        Require(
            Result.Status == ETSPipelineDispatchStatus::Dispatched,
            "Authorized Chat ready must produce a dispatch"
        );
        Require(Result.Dispatch.has_value(), "Dispatched result must own a dispatch");
        Require(
            Result.Dispatch->Emission.EmissionId
                == Admission.EnqueueResult->AdmittedEmission.EmissionId,
            "Dispatch identity must match the admitted emission"
        );
        Require(
            Result.Dispatch->Emission.Flow == ETSEventFlow::Chat,
            "Chat dispatch must preserve the Chat flow"
        );
        RequireChatInputEqual(
            Result.Dispatch->Payload.Input,
            Input,
            "Owned Chat processing dispatch"
        );

        Require(
            Coordinator.VisitEmissionBinding(
                Result.Dispatch->Emission.EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Dispatched Chat binding must enter Processing"
                    );
                }
            ),
            "Dispatched Chat binding must remain stored"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                Result.Dispatch->Emission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Stored payload after authorized dispatch"
                    );
                }
            ),
            "Dispatched Chat payload must remain stored"
        );
        Require(Coordinator.GetBindingCount() == 1, "Dispatch must retain one binding");
        Require(
            Coordinator.GetChatPayloadCount() == 1,
            "Dispatch must retain one Chat payload"
        );
    }

    void TestPendingChatCannotReplaceAuthorizedReady()
    {
        FTSEventPipelineCoordinator Coordinator;
        FTSChatInput FirstInput = MakeCompleteInput();
        FirstInput.Comment = "Authorized InFlight Chat";
        FTSChatInput SecondInput = MakeCompleteInput();
        SecondInput.Comment = "Pending Chat without ready";

        const FTSPipelineAdmissionResult First =
            Coordinator.SubmitChat(FirstInput);
        const FTSPipelineAdmissionResult Second =
            Coordinator.SubmitChat(SecondInput);

        Require(
            First.Status == ETSPipelineAdmissionStatus::Accepted &&
                First.EnqueueResult.has_value(),
            "First ready-retention admission must succeed"
        );
        Require(
            Second.Status == ETSPipelineAdmissionStatus::Accepted &&
                Second.EnqueueResult.has_value(),
            "Second pending admission must succeed"
        );
        Require(
            First.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::EmissionReady,
            "First admission must authorize a ready"
        );
        Require(
            Second.EnqueueResult->AutoPumpOutcome.Status
                == ETSPumpStatus::NotRequested,
            "Second admission must not replace the pending ready"
        );

        const FTSEmissionId FirstEmissionId =
            First.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSEmissionId SecondEmissionId =
            Second.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSChatDispatchResult Dispatch =
            Coordinator.BeginChatProcessing();

        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "The retained ready must still dispatch"
        );
        Require(
            Dispatch.Dispatch->Emission.EmissionId == FirstEmissionId,
            "NotRequested must preserve authorization for the first emission"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstEmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Authorized first binding must enter Processing"
                    );
                }
            ),
            "First binding must remain available after dispatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondEmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Pending second binding must remain Bound"
                    );
                }
            ),
            "Second binding must remain available"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                SecondEmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        SecondInput,
                        "Pending second Chat payload"
                    );
                }
            ),
            "Pending second payload must remain intact"
        );
        Require(Coordinator.GetBindingCount() == 2, "Both bindings must remain stored");
        Require(
            Coordinator.GetChatPayloadCount() == 2,
            "Both Chat payloads must remain stored"
        );
    }

    void TestChatDispatchPayloadIsIndependentCopy()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Independent dispatch setup admission must succeed"
        );

        FTSChatDispatchResult Dispatch = Coordinator.BeginChatProcessing();
        Require(
            Dispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                Dispatch.Dispatch.has_value(),
            "Independent copy test requires a dispatch"
        );

        Dispatch.Dispatch->Payload.Input.Comment = "Mutated dispatch comment";
        Dispatch.Dispatch->Payload.Input.Emotes.clear();
        Dispatch.Dispatch->Payload.Input.User.Nickname = "Mutated dispatch user";
        Dispatch.Dispatch->Payload.Input.User.UniqueId = "mutated-user-id";

        Require(
            Coordinator.VisitChatPayloadForEmission(
                Admission.EnqueueResult->AdmittedEmission.EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Stored payload after dispatch mutation"
                    );
                }
            ),
            "Stored payload must remain available after dispatch mutation"
        );
    }

    void TestChatReadyDispatchIsConsumedOnce()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSChatInput Input = MakeCompleteInput();
        const FTSPipelineAdmissionResult Admission =
            Coordinator.SubmitChat(Input);
        Require(
            Admission.Status == ETSPipelineAdmissionStatus::Accepted &&
                Admission.EnqueueResult.has_value(),
            "Single-consumption setup admission must succeed"
        );

        const FTSEmissionId EmissionId =
            Admission.EnqueueResult->AdmittedEmission.EmissionId;
        const FTSChatDispatchResult FirstDispatch =
            Coordinator.BeginChatProcessing();
        Require(
            FirstDispatch.Status == ETSPipelineDispatchStatus::Dispatched &&
                FirstDispatch.Dispatch.has_value(),
            "First BeginChatProcessing must dispatch"
        );

        const std::size_t BindingCount = Coordinator.GetBindingCount();
        const std::size_t PayloadCount = Coordinator.GetChatPayloadCount();
        const FTSChatDispatchResult SecondDispatch =
            Coordinator.BeginChatProcessing();

        Require(
            SecondDispatch.Status == ETSPipelineDispatchStatus::NoEmissionReady,
            "Consumed ready must not dispatch twice"
        );
        Require(
            !SecondDispatch.Dispatch.has_value(),
            "Second BeginChatProcessing must not contain a dispatch"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                EmissionId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Processing,
                        "Consumed ready binding must remain Processing"
                    );
                }
            ),
            "Consumed ready binding must remain stored"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                EmissionId,
                [&](const FTSChatPayload& Payload)
                {
                    RequireChatInputEqual(
                        Payload.Input,
                        Input,
                        "Payload after single ready consumption"
                    );
                }
            ),
            "Consumed ready payload must remain stored"
        );
        Require(
            Coordinator.GetBindingCount() == BindingCount,
            "Second dispatch attempt must not change binding count"
        );
        Require(
            Coordinator.GetChatPayloadCount() == PayloadCount,
            "Second dispatch attempt must not change payload count"
        );
    }

    void TestSuccessfulChatCompletionCleansTerminalState()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Successful completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Successful completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.EmissionId == EmissionId &&
                Completion.ProcessingResult == ETSProcessingResult::Succeeded,
            "Successful completion metadata mismatch"
        );
        Require(
            Completion.ConfirmResult.has_value() &&
                !Completion.CancelResult.has_value(),
            "Succeeded must expose only ConfirmResult"
        );
        Require(
            Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed,
            "Succeeded must be confirmed by the core"
        );
        Require(
            !Completion.ConfirmResult->LifecycleEvents.empty() &&
                Completion.ConfirmResult->LifecycleEvents.front()
                        .Envelope.EmissionId == EmissionId &&
                Completion.ConfirmResult->LifecycleEvents.front().Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Succeeded lifecycle must start with its Confirmed event"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Successful terminal handling must clean binding and payload"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Successful cleanup must not leave a ready notification"
        );
    }

    void TestSuccessfulChatCompletionCapturesNextReady()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "First successful Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Second successful Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "The first Chat must enter Processing"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.ConfirmResult.has_value() &&
                Completion.ConfirmResult->AutoPumpOutcome.Status ==
                    ETSPumpStatus::EmissionReady &&
                Completion.ConfirmResult->AutoPumpOutcome.ReadyEmission
                        .EmissionId == SecondId,
            "Confirm Auto Pump must authorize the second Chat"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1,
            "Only the second Chat must remain stored"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Bound,
                        "Auto Pump ready binding must remain Bound"
                    );
                }
            ),
            "Second Chat binding must remain available"
        );
        Require(
            Coordinator.VisitChatPayloadForEmission(
                SecondId,
                [](const FTSChatPayload&)
                {
                }
            ),
            "Second Chat payload must remain available"
        );
        Require(
            BeginReadyChat(Coordinator) == SecondId,
            "Captured Confirm Auto Pump ready must dispatch once"
        );
    }

    void TestConfirmProcessesTrailingExpirationInCoreOrder()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                true,
                true,
                5s,
                ETSEventExpirePolicy::Discard
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "InFlight before expiration");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Pending expiration");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing before the TTL boundary"
        );

        Clock.Advance(5s);
        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );

        Require(
            Completion.ConfirmResult.has_value(),
            "Confirm expiration scenario must expose ConfirmResult"
        );
        const FTSConfirmResult& ConfirmResult = *Completion.ConfirmResult;
        Require(
            ConfirmResult.LifecycleEvents.size() == 2,
            "Confirm must report its terminal followed by pending expiration"
        );
        Require(
            ConfirmResult.LifecycleEvents[0].Envelope.EmissionId == FirstId &&
                ConfirmResult.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Confirmed,
            "Confirm lifecycle must begin with the InFlight terminal"
        );
        Require(
            ConfirmResult.LifecycleEvents[1].Envelope.EmissionId == SecondId &&
                ConfirmResult.LifecycleEvents[1].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Confirm lifecycle must preserve the trailing expiration"
        );
        Require(
            ConfirmResult.AutoPumpOutcome.Status ==
                ETSPumpStatus::QueueEmpty,
            "Expired pending Chat must leave Confirm Auto Pump empty"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Confirmed and expired Chats must both be cleaned"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Expiration during Confirm must not leave a ready notification"
        );
    }

    void TestCancelledChatCompletionCleansTerminalState()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Cancelled completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Cancelled completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Cancelled
            );

        Require(
            Completion.ProcessingResult == ETSProcessingResult::Cancelled &&
                !Completion.ConfirmResult.has_value() &&
                Completion.CancelResult.has_value(),
            "Cancelled must expose only CancelResult"
        );
        Require(
            Completion.CancelResult->Status ==
                ETSCancelInFlightStatus::Cancelled &&
                Completion.CancelResult->LifecycleEvents.size() == 1 &&
                Completion.CancelResult->LifecycleEvents[0]
                        .Envelope.EmissionId == EmissionId &&
                Completion.CancelResult->LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Cancelled,
            "Cancelled must report exactly its core terminal"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Cancelled terminal handling must clean binding and payload"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Cancel must not produce an automatic ready"
        );
    }

    void TestFailedChatCompletionUsesCancellationTerminal()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Failed completion");
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Failed completion must begin the admitted Chat"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                EmissionId,
                ETSProcessingResult::Failed
            );

        Require(
            Completion.ProcessingResult == ETSProcessingResult::Failed &&
                !Completion.ConfirmResult.has_value() &&
                Completion.CancelResult.has_value(),
            "Failed must preserve Failed and expose only CancelResult"
        );
        Require(
            Completion.CancelResult->Status ==
                ETSCancelInFlightStatus::Cancelled &&
                Completion.CancelResult->LifecycleEvents.size() == 1 &&
                Completion.CancelResult->LifecycleEvents[0]
                        .Envelope.EmissionId == EmissionId &&
                Completion.CancelResult->LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::Cancelled,
            "Failed must use the same terminal core cancellation"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Failed terminal handling must clean binding and payload"
        );
    }

    void TestCompletingBoundChatFailsBeforeCoreMutation()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "Processing Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Still Bound Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing"
        );

        bool bThrew = false;
        try
        {
            (void)Coordinator.CompleteChatProcessing(
                SecondId,
                ETSProcessingResult::Succeeded
            );
        }
        catch (const std::logic_error&)
        {
            bThrew = true;
        }

        Require(bThrew, "Completing a Bound Chat must throw logic_error");
        Require(
            Coordinator.GetBindingCount() == 2 &&
                Coordinator.GetChatPayloadCount() == 2,
            "Rejected completion must preserve both external authorities"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                FirstId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState ==
                            ETSExternalEmissionState::Processing,
                        "Rejected completion must preserve Processing"
                    );
                }
            ),
            "Processing binding must remain available"
        );
        Require(
            Coordinator.VisitEmissionBinding(
                SecondId,
                [](const FTSEmissionBinding& Binding)
                {
                    Require(
                        Binding.ExternalState == ETSExternalEmissionState::Bound,
                        "Rejected completion must preserve Bound"
                    );
                }
            ),
            "Bound binding must remain available"
        );

        const FTSChatProcessingCompletionResult FirstCompletion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Succeeded
            );
        Require(
            FirstCompletion.ConfirmResult.has_value() &&
                FirstCompletion.ConfirmResult->Status ==
                    ETSConfirmStatus::Confirmed,
            "Original InFlight Chat must remain confirmable"
        );
    }

    void TestCancelRequiresExplicitPumpForNextChat()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId FirstId =
            SubmitAcceptedChat(Coordinator, "Cancelled first Chat");
        const FTSEmissionId SecondId =
            SubmitAcceptedChat(Coordinator, "Pending second Chat");
        Require(
            BeginReadyChat(Coordinator) == FirstId,
            "First Chat must enter Processing"
        );

        const FTSChatProcessingCompletionResult Completion =
            Coordinator.CompleteChatProcessing(
                FirstId,
                ETSProcessingResult::Cancelled
            );
        Require(
            Completion.CancelResult.has_value(),
            "Cancelled first Chat must expose CancelResult"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Cancel must not Auto Pump the pending Chat"
        );

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::EmissionReady &&
                PumpResult.Outcome.ReadyEmission.EmissionId == SecondId,
            "Explicit Pump must authorize the pending Chat"
        );
        Require(
            BeginReadyChat(Coordinator) == SecondId,
            "Explicitly pumped Chat must dispatch"
        );
    }

    void TestBusyPumpPreservesPendingReadyNotification()
    {
        FTSEventPipelineCoordinator Coordinator;
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Ready before busy Pump");

        const FTSPumpResult PumpResult = Coordinator.Pump();
        Require(
            PumpResult.Outcome.Status == ETSPumpStatus::Busy &&
                PumpResult.LifecycleEvents.empty(),
            "Pump must report Busy while the ready emission is InFlight"
        );
        Require(
            BeginReadyChat(Coordinator) == EmissionId,
            "Busy Pump must preserve the pending ready notification"
        );
    }

    void TestDiscardExpirationThroughCoordinator()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                false,
                true,
                5s,
                ETSEventExpirePolicy::Discard
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Discard at TTL");

        const FTSNextWakeTimeResult Wake = Coordinator.GetNextWakeTime();
        Require(
            Wake.Status == ETSNextWakeStatus::WakeScheduled &&
                Wake.WakeTime == Clock.Now + 5s,
            "Coordinator must expose the exact Chat wake time"
        );

        Clock.Advance(5s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents[0].Envelope.EmissionId ==
                    EmissionId &&
                Expirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredDiscard,
            "Discard expiration must be forwarded intact"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Discard expiration must clean binding and payload"
        );
        Require(
            Coordinator.GetNextWakeTime().Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Discard cleanup must remove the wake schedule"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Discard expiration must not create a ready notification"
        );
    }

    void TestConsolidateExpirationCleansChatWithoutAccumulation()
    {
        FControlledClock Clock;
        FTSEventPipelineCoordinator Coordinator(
            MakeOperationalChatSettings(
                false,
                true,
                5s,
                ETSEventExpirePolicy::Consolidate
            ),
            Clock.MakeProvider()
        );
        const FTSEmissionId EmissionId =
            SubmitAcceptedChat(Coordinator, "Consolidate at TTL");

        Clock.Advance(5s);
        const FTSProcessDueExpirationsResult Expirations =
            Coordinator.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1 &&
                Expirations.LifecycleEvents[0].Envelope.EmissionId ==
                    EmissionId &&
                Expirations.LifecycleEvents[0].Reason ==
                    ETSEmissionTerminalReason::ExpiredConsolidate,
            "Consolidate expiration must be forwarded intact"
        );
        Require(
            Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Consolidate currently cleans Chat like discard"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::NoEmissionReady,
            "Consolidate expiration must not create a ready notification"
        );

        const FTSProcessDueExpirationsResult Repeated =
            Coordinator.ProcessDueExpirations();
        Require(
            Repeated.LifecycleEvents.empty() &&
                Coordinator.GetBindingCount() == 0 &&
                Coordinator.GetChatPayloadCount() == 0,
            "Consolidate must not accumulate or repeat terminal state"
        );
    }

    using FTestFunction = void (*)();

    struct FTestCase
    {
        const char* Name = nullptr;
        FTestFunction Function = nullptr;
    };
}

int main()
{
    const std::vector<FTestCase> Tests{
        {"Chat candidate and admission defaults", &TestChatCandidateAndAdmissionDefaults},
        {"Chat payload snapshot and input preservation", &TestChatPayloadSnapshotAndInputPreservation},
        {"Typed repository stores independent snapshots", &TestTypedRepositoryStoresIndependentSnapshots},
        {"Typed repository erase and handle invariants", &TestTypedRepositoryEraseAndHandleInvariants},
        {"Binding registry insert, visit and duplicate protection", &TestBindingRegistryInsertVisitAndDuplicateProtection},
        {"Binding registry rejects invalid bindings", &TestBindingRegistryRejectsInvalidBindings},
        {"Binding registry conditional transitions", &TestBindingRegistryConditionalTransitions},
        {"Binding registry erase and size", &TestBindingRegistryEraseAndSize},
        {"Chat coordinator first admission Auto Pumps", &TestChatCoordinatorFirstAdmissionAutoPumps},
        {"Chat coordinator second admission while busy", &TestChatCoordinatorSecondAdmissionWhileBusy},
        {"Chat coordinator rejects disabled flow", &TestChatCoordinatorRejectsDisabledFlow},
        {"Chat coordinator capacity rejection preserves prior admission", &TestChatCoordinatorCapacityRejectionPreservesPriorAdmission},
        {"Chat coordinator unknown emission inspection", &TestChatCoordinatorUnknownEmissionInspection},
        {"Chat dispatch without pending ready", &TestChatDispatchWithoutPendingReady},
        {"Authorized Chat ready produces owned dispatch", &TestAuthorizedChatReadyProducesOwnedDispatch},
        {"Pending Chat cannot replace authorized ready", &TestPendingChatCannotReplaceAuthorizedReady},
        {"Chat dispatch payload is an independent copy", &TestChatDispatchPayloadIsIndependentCopy},
        {"Chat ready dispatch is consumed once", &TestChatReadyDispatchIsConsumedOnce},
        {"Successful Chat completion cleans terminal state", &TestSuccessfulChatCompletionCleansTerminalState},
        {"Successful Chat completion captures next ready", &TestSuccessfulChatCompletionCapturesNextReady},
        {"Confirm processes trailing expiration in core order", &TestConfirmProcessesTrailingExpirationInCoreOrder},
        {"Cancelled Chat completion cleans terminal state", &TestCancelledChatCompletionCleansTerminalState},
        {"Failed Chat completion uses cancellation terminal", &TestFailedChatCompletionUsesCancellationTerminal},
        {"Completing Bound Chat fails before core mutation", &TestCompletingBoundChatFailsBeforeCoreMutation},
        {"Cancel requires explicit Pump for next Chat", &TestCancelRequiresExplicitPumpForNextChat},
        {"Busy Pump preserves pending ready notification", &TestBusyPumpPreservesPendingReadyNotification},
        {"Discard expiration through coordinator", &TestDiscardExpirationThroughCoordinator},
        {"Consolidate expiration cleans without accumulation", &TestConsolidateExpirationCleansChatWithoutAccumulation}
    };

    std::size_t PassedCount = 0;
    std::size_t FailedCount = 0;

    for (const FTestCase& Test : Tests)
    {
        try
        {
            Test.Function();
            ++PassedCount;
            std::cout << "PASS: " << Test.Name << '\n';
        }
        catch (const std::exception& Error)
        {
            ++FailedCount;
            std::cerr
                << "FAIL: " << Test.Name
                << " - " << Error.what()
                << '\n';
        }
        catch (...)
        {
            ++FailedCount;
            std::cerr
                << "FAIL: " << Test.Name
                << " - unknown exception"
                << '\n';
        }
    }

    std::cout
        << "RESULT: " << PassedCount << " passed, "
        << FailedCount << " failed\n";

    return FailedCount == 0 ? 0 : 1;
}
