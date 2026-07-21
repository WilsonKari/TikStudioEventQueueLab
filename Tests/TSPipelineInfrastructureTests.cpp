#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"
#include "EventPipeline/Repositories/TSFollowPayloadRepository.h"
#include "TSPipelineTestSupport.h"
#include "TSTestSuites.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>


static_assert(!std::is_copy_constructible_v<FTSChatPayloadRepository>);
static_assert(!std::is_copy_assignable_v<FTSChatPayloadRepository>);
static_assert(!std::is_move_constructible_v<FTSChatPayloadRepository>);
static_assert(!std::is_move_assignable_v<FTSChatPayloadRepository>);

static_assert(!std::is_copy_constructible_v<FTSFollowPayloadRepository>);
static_assert(!std::is_copy_assignable_v<FTSFollowPayloadRepository>);
static_assert(!std::is_move_constructible_v<FTSFollowPayloadRepository>);
static_assert(!std::is_move_assignable_v<FTSFollowPayloadRepository>);

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
    using namespace TikStudio::Tests;

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

    void TestCoordinatorDelegatesFlowSettingsUpdateWithoutPipelineMutation()
    {
        FTSEventQueueSettings Settings;
        FTSFlowQueueSettings* ChatSettings =
            Settings.TryGetFlowSettings(ETSEventFlow::Chat);
        Require(ChatSettings != nullptr, "Coordinator Chat settings must exist");
        ChatSettings->bEnabled = true;
        ChatSettings->TTL = std::chrono::milliseconds{0};
        ChatSettings->MaxSlots = 10;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = false;
        Settings.Pump.bPumpAfterConfirm = false;

        FTSEventPipelineCoordinator Coordinator(Settings);
        const FTSPipelineAdmissionResult Existing =
            Coordinator.SubmitChat(MakeCompleteInput());
        Require(
            Existing.Status == ETSPipelineAdmissionStatus::Accepted &&
                Existing.EnqueueResult.has_value(),
            "Coordinator settings update setup admission failed"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Coordinator setup authorities mismatch"
        );

        FTSFlowQueueSettings InvalidSettings = *ChatSettings;
        InvalidSettings.TTL = std::chrono::milliseconds{-1};
        Require(
            Coordinator.UpdateFlowSettings(
                ETSEventFlow::Chat,
                InvalidSettings
            ).Status == ETSUpdateFlowSettingsStatus::RejectedInvalidTTL,
            "Coordinator must preserve the Core rejection status"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Rejected settings update must not alter Pipeline authorities"
        );

        FTSFlowQueueSettings DisabledSettings = *ChatSettings;
        DisabledSettings.bEnabled = false;
        const FTSUpdateFlowSettingsResult Update =
            Coordinator.UpdateFlowSettings(
                ETSEventFlow::Chat,
                DisabledSettings
            );
        Require(
            Update.Status == ETSUpdateFlowSettingsStatus::Updated &&
                Update.Flow == ETSEventFlow::Chat,
            "Coordinator must return the Core update result"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1 &&
                !Coordinator.PeekPendingReadyFamilyKind().has_value(),
            "Valid settings update must not alter Pipeline authorities"
        );

        const FTSPipelineAdmissionResult Rejected =
            Coordinator.SubmitChat(MakeCompleteInput());
        Require(
            Rejected.Status == ETSPipelineAdmissionStatus::RejectedByCore &&
                Rejected.EnqueueResult.has_value() &&
                Rejected.EnqueueResult->Status ==
                    ETSEnqueueStatus::RejectedDisabled,
            "Updated settings must affect the later Coordinator admission"
        );
        Require(
            Coordinator.GetBindingCount() == 1 &&
                Coordinator.GetChatPayloadCount() == 1,
            "Rejected later admission must roll back its provisional payload"
        );

        const FTSPumpResult Pump = Coordinator.Pump();
        Require(
            Pump.Outcome.Status == ETSPumpStatus::EmissionReady &&
                Pump.Outcome.ReadyEmission.EmissionId ==
                    Existing.EnqueueResult->AdmittedEmission.EmissionId,
            "Existing Coordinator emission must remain selectable"
        );
        Require(
            Coordinator.BeginChatProcessing().Status ==
                ETSPipelineDispatchStatus::Dispatched,
            "Existing Coordinator emission must remain dispatchable"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterPipelineInfrastructureTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Typed repository stores independent snapshots", &TestTypedRepositoryStoresIndependentSnapshots});
        Tests.push_back({"Typed repository erase and handle invariants", &TestTypedRepositoryEraseAndHandleInvariants});
        Tests.push_back({"Binding registry insert, visit and duplicate protection", &TestBindingRegistryInsertVisitAndDuplicateProtection});
        Tests.push_back({"Binding registry rejects invalid bindings", &TestBindingRegistryRejectsInvalidBindings});
        Tests.push_back({"Binding registry conditional transitions", &TestBindingRegistryConditionalTransitions});
        Tests.push_back({"Binding registry erase and size", &TestBindingRegistryEraseAndSize});
        Tests.push_back({"Coordinator delegates flow settings update", &TestCoordinatorDelegatesFlowSettingsUpdateWithoutPipelineMutation});
    }
}
