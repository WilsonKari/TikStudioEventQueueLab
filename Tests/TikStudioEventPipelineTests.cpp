#include "EventPipeline/Bindings/TSEmissionBindingRegistry.h"
#include "EventPipeline/Families/TSChatFamily.h"
#include "EventPipeline/Repositories/TSChatPayloadRepository.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
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
        {"Binding registry erase and size", &TestBindingRegistryEraseAndSize}
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
