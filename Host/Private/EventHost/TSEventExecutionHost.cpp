#include "EventHost/TSEventExecutionHost.h"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

static_assert(
    std::is_nothrow_move_constructible_v<FTSEventHostCycleResult>
);

static_assert(!std::is_copy_constructible_v<FTSEventExecutionHost>);
static_assert(!std::is_copy_assignable_v<FTSEventExecutionHost>);
static_assert(!std::is_move_constructible_v<FTSEventExecutionHost>);
static_assert(!std::is_move_assignable_v<FTSEventExecutionHost>);

namespace
{
    template <typename... TFunctions>
    struct TOverloaded final : TFunctions...
    {
        using TFunctions::operator()...;
    };

    template <typename... TFunctions>
    TOverloaded(TFunctions...) -> TOverloaded<TFunctions...>;

    [[nodiscard]]
    constexpr bool IsValidProcessingResult(
        ETSProcessingResult ProcessingResult
    ) noexcept
    {
        return ProcessingResult == ETSProcessingResult::Succeeded ||
            ProcessingResult == ETSProcessingResult::Cancelled ||
            ProcessingResult == ETSProcessingResult::Failed;
    }

    void ValidateCompletionArguments(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    )
    {
        if (EmissionId == 0)
        {
            throw std::invalid_argument(
                "Completion requires a non-zero EmissionId"
            );
        }

        if (!IsValidProcessingResult(ProcessingResult))
        {
            throw std::invalid_argument(
                "Completion has an invalid processing result"
            );
        }
    }

    template <typename TDispatch>
    void ValidateDispatchResult(
        const TTSPipelineDispatchResult<TDispatch>& DispatchResult
    )
    {
        switch (DispatchResult.Status)
        {
        case ETSPipelineDispatchStatus::Dispatched:
            if (!DispatchResult.Dispatch.has_value())
            {
                throw std::logic_error(
                    "Dispatched result has no owned dispatch"
                );
            }
            return;

        case ETSPipelineDispatchStatus::NoEmissionReady:
            if (DispatchResult.Dispatch.has_value())
            {
                throw std::logic_error(
                    "NoEmissionReady result contains a dispatch"
                );
            }
            return;
        }

        throw std::logic_error("Dispatch result has an unknown status");
    }

    void ValidateOwnedDispatch(
        const FTSChatProcessingDispatch& Dispatch
    )
    {
        if (Dispatch.Emission.EmissionId == 0 ||
            Dispatch.Emission.Flow != ETSEventFlow::Chat)
        {
            throw std::logic_error("Invalid owned Chat dispatch");
        }
    }

    void ValidateOwnedDispatch(
        const FTSFollowProcessingDispatch& Dispatch
    )
    {
        if (Dispatch.Emission.EmissionId == 0 ||
            Dispatch.Emission.Flow != ETSEventFlow::Follow)
        {
            throw std::logic_error("Invalid owned Follow dispatch");
        }
    }

    [[nodiscard]]
    FTSEmissionId GetDispatchEmissionId(
        const FTSEventProcessingDispatch& Dispatch
    )
    {
        return std::visit(
            [](const auto& TypedDispatch)
            {
                return TypedDispatch.Emission.EmissionId;
            },
            Dispatch
        );
    }
}

class FTSEventExecutionHost::FImpl final
{
public:
    FImpl(
        FTSEventQueueSettings Settings,
        FTSNowProvider NowProvider
    )
        : Coordinator(std::move(Settings), std::move(NowProvider))
        , OwnerThreadId(std::this_thread::get_id())
    {
    }

    [[nodiscard]]
    bool PostChat(FTSChatInput Input)
    {
        return PostCommand(std::move(Input));
    }

    [[nodiscard]]
    bool PostFollow(FTSFollowInput Input)
    {
        return PostCommand(std::move(Input));
    }

    [[nodiscard]]
    bool PostChatCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    )
    {
        // La familia del ID se valida en el owner thread, donde vive el Coordinator.
        ValidateCompletionArguments(EmissionId, ProcessingResult);
        return PostCommand(
            FPostedChatCompletion{EmissionId, ProcessingResult}
        );
    }

    [[nodiscard]]
    bool PostFollowCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    )
    {
        // La familia del ID se valida en el owner thread, donde vive el Coordinator.
        ValidateCompletionArguments(EmissionId, ProcessingResult);
        return PostCommand(
            FPostedFollowCompletion{EmissionId, ProcessingResult}
        );
    }

    [[nodiscard]]
    FTSEventHostCycleResult RunOneCycle()
    {
        if (std::this_thread::get_id() != OwnerThreadId)
        {
            throw std::logic_error(
                "RunOneCycle must execute on the Host owner thread"
            );
        }

        std::optional<FPostedCommand> PostedCommand;
        {
            std::lock_guard<std::mutex> Lock(CommandMutex);
            if (!PostedCommands.empty())
            {
                PostedCommand.emplace(std::move(PostedCommands.front()));
                PostedCommands.pop_front();
            }
        }

        FTSEventHostCycleResult Result;
        if (PostedCommand.has_value())
        {
            ProcessPostedCommand(std::move(*PostedCommand), Result);
        }

        Result.DueExpirations = Coordinator.ProcessDueExpirations();

        Result.Dispatch = TryBeginPendingDispatch();
        if (!Result.Dispatch.has_value())
        {
            Result.PumpResult.emplace(Coordinator.Pump());
            const FTSPumpOutcome& PumpOutcome = Result.PumpResult->Outcome;

            switch (PumpOutcome.Status)
            {
            case ETSPumpStatus::EmissionReady:
            {
                if (PumpOutcome.ReadyEmission.EmissionId == 0)
                {
                    throw std::logic_error(
                        "Host Pump produced an invalid ready identity"
                    );
                }

                // Un ready creado por Pump debe salir en este ciclo; dejarlo pendiente
                // rompería la garantía work-conserving del Host.
                Result.Dispatch = TryBeginPendingDispatch();
                if (!Result.Dispatch.has_value() ||
                    GetDispatchEmissionId(*Result.Dispatch) !=
                        PumpOutcome.ReadyEmission.EmissionId)
                {
                    throw std::logic_error(
                        "Host Pump ready does not match its dispatch"
                    );
                }
                break;
            }

            case ETSPumpStatus::Busy:
            case ETSPumpStatus::QueueEmpty:
                break;

            case ETSPumpStatus::NotRequested:
                throw std::logic_error(
                    "Public Host Pump returned NotRequested"
                );

            default:
                throw std::logic_error(
                    "Public Host Pump returned an unknown status"
                );
            }
        }

        Result.NextWakeTime = Coordinator.GetNextWakeTime();

        {
            std::lock_guard<std::mutex> Lock(CommandMutex);
            Result.bMoreCommandsPending = !PostedCommands.empty();
        }

        return Result;
    }

private:
    struct FPostedChatCompletion
    {
        FTSEmissionId EmissionId = 0;
        ETSProcessingResult ProcessingResult =
            ETSProcessingResult::Failed;
    };

    struct FPostedFollowCompletion
    {
        FTSEmissionId EmissionId = 0;
        ETSProcessingResult ProcessingResult =
            ETSProcessingResult::Failed;
    };

    using FPostedCommand = std::variant<
        FTSChatInput,
        FTSFollowInput,
        FPostedChatCompletion,
        FPostedFollowCompletion
    >;

    static_assert(
        std::is_nothrow_move_constructible_v<FPostedCommand>
    );

    template <typename TCommand>
    [[nodiscard]]
    bool PostCommand(TCommand Command)
    {
        std::lock_guard<std::mutex> Lock(CommandMutex);
        const bool bWasEmpty = PostedCommands.empty();

        // Una sola bandeja conserva el orden total de publicaciones entre familias.
        PostedCommands.emplace_back(std::move(Command));
        return bWasEmpty;
    }

    void ProcessPostedCommand(
        FPostedCommand PostedCommand,
        FTSEventHostCycleResult& Result
    )
    {
        std::visit(
            TOverloaded{
                [&](FTSChatInput& Input)
                {
                    Result.ProcessedCommand =
                        ETSEventHostCommandKind::ChatInput;
                    Result.AdmissionResult.emplace(
                        Coordinator.SubmitChat(std::move(Input))
                    );
                },
                [&](FTSFollowInput& Input)
                {
                    Result.ProcessedCommand =
                        ETSEventHostCommandKind::FollowInput;
                    Result.AdmissionResult.emplace(
                        Coordinator.SubmitFollow(std::move(Input))
                    );
                },
                [&](const FPostedChatCompletion& Completion)
                {
                    Result.ProcessedCommand =
                        ETSEventHostCommandKind::ChatCompletion;
                    Result.CompletionResult.emplace(
                        Coordinator.CompleteChatProcessing(
                            Completion.EmissionId,
                            Completion.ProcessingResult
                        )
                    );
                },
                [&](const FPostedFollowCompletion& Completion)
                {
                    Result.ProcessedCommand =
                        ETSEventHostCommandKind::FollowCompletion;
                    Result.CompletionResult.emplace(
                        Coordinator.CompleteFollowProcessing(
                            Completion.EmissionId,
                            Completion.ProcessingResult
                        )
                    );
                }
            },
            PostedCommand
        );
    }

    [[nodiscard]]
    std::optional<FTSEventProcessingDispatch>
    TryBeginPendingDispatch()
    {
        const std::optional<ETSEventFamilyKind> FamilyKind =
            Coordinator.PeekPendingReadyFamilyKind();
        if (!FamilyKind.has_value())
        {
            return std::nullopt;
        }

        // Peek decide la ruta; sólo el Begin tipado correcto puede consumir el ready.
        switch (*FamilyKind)
        {
        case ETSEventFamilyKind::Chat:
        {
            FTSChatDispatchResult DispatchResult =
                Coordinator.BeginChatProcessing();
            ValidateDispatchResult(DispatchResult);
            if (DispatchResult.Status !=
                ETSPipelineDispatchStatus::Dispatched)
            {
                throw std::logic_error(
                    "Peeked Chat ready did not produce a dispatch"
                );
            }

            ValidateOwnedDispatch(*DispatchResult.Dispatch);
            std::optional<FTSEventProcessingDispatch> Dispatch;
            Dispatch.emplace(
                std::in_place_type<FTSChatProcessingDispatch>,
                std::move(*DispatchResult.Dispatch)
            );
            return Dispatch;
        }

        case ETSEventFamilyKind::Follow:
        {
            FTSFollowDispatchResult DispatchResult =
                Coordinator.BeginFollowProcessing();
            ValidateDispatchResult(DispatchResult);
            if (DispatchResult.Status !=
                ETSPipelineDispatchStatus::Dispatched)
            {
                throw std::logic_error(
                    "Peeked Follow ready did not produce a dispatch"
                );
            }

            ValidateOwnedDispatch(*DispatchResult.Dispatch);
            std::optional<FTSEventProcessingDispatch> Dispatch;
            Dispatch.emplace(
                std::in_place_type<FTSFollowProcessingDispatch>,
                std::move(*DispatchResult.Dispatch)
            );
            return Dispatch;
        }

        default:
            throw std::logic_error(
                "Pending ready belongs to an unsupported Host family"
            );
        }
    }

    FTSEventPipelineCoordinator Coordinator;
    std::mutex CommandMutex;
    std::deque<FPostedCommand> PostedCommands;
    std::thread::id OwnerThreadId;
};

FTSEventExecutionHost::FTSEventExecutionHost(
    FTSEventQueueSettings Settings,
    FTSNowProvider NowProvider
)
    : Impl(std::make_unique<FImpl>(
        std::move(Settings),
        std::move(NowProvider)
    ))
{
}

FTSEventExecutionHost::~FTSEventExecutionHost() = default;

bool FTSEventExecutionHost::PostChat(FTSChatInput Input)
{
    return Impl->PostChat(std::move(Input));
}

bool FTSEventExecutionHost::PostFollow(FTSFollowInput Input)
{
    return Impl->PostFollow(std::move(Input));
}

bool FTSEventExecutionHost::PostChatCompletion(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return Impl->PostChatCompletion(EmissionId, ProcessingResult);
}

bool FTSEventExecutionHost::PostFollowCompletion(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return Impl->PostFollowCompletion(EmissionId, ProcessingResult);
}

FTSEventHostCycleResult FTSEventExecutionHost::RunOneCycle()
{
    return Impl->RunOneCycle();
}
