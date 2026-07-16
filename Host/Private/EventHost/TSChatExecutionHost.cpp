#include "EventHost/TSChatExecutionHost.h"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

static_assert(
    std::is_nothrow_move_constructible_v<FTSChatHostCycleResult>
);

static_assert(!std::is_copy_constructible_v<FTSChatExecutionHost>);
static_assert(!std::is_copy_assignable_v<FTSChatExecutionHost>);
static_assert(!std::is_move_constructible_v<FTSChatExecutionHost>);
static_assert(!std::is_move_assignable_v<FTSChatExecutionHost>);

namespace
{
    [[nodiscard]]
    constexpr bool IsValidProcessingResult(
        ETSProcessingResult ProcessingResult
    ) noexcept
    {
        return ProcessingResult == ETSProcessingResult::Succeeded ||
            ProcessingResult == ETSProcessingResult::Cancelled ||
            ProcessingResult == ETSProcessingResult::Failed;
    }

    void ValidateDispatchResult(
        const FTSChatDispatchResult& DispatchResult
    )
    {
        switch (DispatchResult.Status)
        {
        case ETSPipelineDispatchStatus::Dispatched:
            if (!DispatchResult.Dispatch.has_value())
            {
                throw std::logic_error(
                    "Dispatched Chat result has no owned dispatch"
                );
            }
            return;

        case ETSPipelineDispatchStatus::NoEmissionReady:
            if (DispatchResult.Dispatch.has_value())
            {
                throw std::logic_error(
                    "NoEmissionReady Chat result contains a dispatch"
                );
            }
            return;
        }

        throw std::logic_error("Chat dispatch result has an unknown status");
    }
}

class FTSChatExecutionHost::FImpl final
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
        std::lock_guard<std::mutex> Lock(CommandMutex);
        const bool bWasEmpty = PostedCommands.empty();

        // La adquisición del mutex define el orden FIFO entre publicaciones.
        PostedCommands.emplace_back(std::move(Input));
        return bWasEmpty;
    }

    [[nodiscard]]
    bool PostChatCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    )
    {
        if (EmissionId == 0)
        {
            throw std::invalid_argument(
                "Chat completion requires a non-zero EmissionId"
            );
        }

        if (!IsValidProcessingResult(ProcessingResult))
        {
            throw std::invalid_argument(
                "Chat completion has an invalid processing result"
            );
        }

        std::lock_guard<std::mutex> Lock(CommandMutex);
        const bool bWasEmpty = PostedCommands.empty();
        PostedCommands.emplace_back(
            FPostedChatCompletion{EmissionId, ProcessingResult}
        );
        return bWasEmpty;
    }

    [[nodiscard]]
    FTSChatHostCycleResult RunOneCycle()
    {
        // Una llamada equivocada no debe observar ni retirar trabajo publicado.
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

        FTSChatHostCycleResult Result;

        if (PostedCommand.has_value())
        {
            if (std::holds_alternative<FTSChatInput>(*PostedCommand))
            {
                Result.ProcessedCommand = ETSChatHostCommandKind::ChatInput;
                Result.AdmissionResult.emplace(
                    Coordinator.SubmitChat(
                        std::move(std::get<FTSChatInput>(*PostedCommand))
                    )
                );
            }
            else
            {
                const FPostedChatCompletion Completion =
                    std::get<FPostedChatCompletion>(*PostedCommand);
                Result.ProcessedCommand =
                    ETSChatHostCommandKind::ProcessingCompletion;
                Result.CompletionResult.emplace(
                    Coordinator.CompleteChatProcessing(
                        Completion.EmissionId,
                        Completion.ProcessingResult
                    )
                );
            }
        }

        // El comando tiene precedencia sobre el mantenimiento temporal del ciclo.
        Result.DueExpirations = Coordinator.ProcessDueExpirations();

        FTSChatDispatchResult DispatchResult =
            Coordinator.BeginChatProcessing();
        ValidateDispatchResult(DispatchResult);

        if (DispatchResult.Status == ETSPipelineDispatchStatus::Dispatched)
        {
            Result.Dispatch.emplace(
                std::move(*DispatchResult.Dispatch)
            );
        }
        else
        {
            Result.PumpResult.emplace(Coordinator.Pump());
            const FTSPumpOutcome& PumpOutcome =
                Result.PumpResult->Outcome;

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

                FTSChatDispatchResult PumpDispatchResult =
                    Coordinator.BeginChatProcessing();
                ValidateDispatchResult(PumpDispatchResult);

                if (PumpDispatchResult.Status !=
                        ETSPipelineDispatchStatus::Dispatched ||
                    PumpDispatchResult.Dispatch->Emission.EmissionId !=
                        PumpOutcome.ReadyEmission.EmissionId)
                {
                    throw std::logic_error(
                        "Host Pump ready does not match its Chat dispatch"
                    );
                }

                Result.Dispatch.emplace(
                    std::move(*PumpDispatchResult.Dispatch)
                );
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

    // Unión cerrada de comandos del Host; nunca cruza hacia Pipeline o Core.
    using FPostedCommand = std::variant<
        FTSChatInput,
        FPostedChatCompletion
    >;

    static_assert(
        std::is_nothrow_move_constructible_v<FPostedCommand>
    );

    FTSEventPipelineCoordinator Coordinator;
    std::mutex CommandMutex;
    std::deque<FPostedCommand> PostedCommands;
    std::thread::id OwnerThreadId;
};

FTSChatExecutionHost::FTSChatExecutionHost(
    FTSEventQueueSettings Settings,
    FTSNowProvider NowProvider
)
    : Impl(std::make_unique<FImpl>(
        std::move(Settings),
        std::move(NowProvider)
    ))
{
}

FTSChatExecutionHost::~FTSChatExecutionHost() = default;

bool FTSChatExecutionHost::PostChat(FTSChatInput Input)
{
    return Impl->PostChat(std::move(Input));
}

bool FTSChatExecutionHost::PostChatCompletion(
    FTSEmissionId EmissionId,
    ETSProcessingResult ProcessingResult
)
{
    return Impl->PostChatCompletion(EmissionId, ProcessingResult);
}

FTSChatHostCycleResult FTSChatExecutionHost::RunOneCycle()
{
    return Impl->RunOneCycle();
}
