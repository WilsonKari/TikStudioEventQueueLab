#include "Shared/TSEventScenarioRunner.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>

namespace
{
    [[nodiscard]]
    const FTSEmissionEnvelope& GetDispatchEnvelope(
        const FTSEventProcessingDispatch& Dispatch
    )
    {
        return std::visit(
            [](const auto& TypedDispatch) -> const FTSEmissionEnvelope&
            {
                return TypedDispatch.Emission;
            },
            Dispatch
        );
    }

    [[nodiscard]]
    bool EnvelopesMatch(
        const FTSEmissionEnvelope& Left,
        const FTSEmissionEnvelope& Right
    ) noexcept
    {
        return Left.EmissionId == Right.EmissionId
            && Left.Flow == Right.Flow
            && Left.Sequence == Right.Sequence
            && Left.CreatedAt == Right.CreatedAt
            && Left.ExpiresAt == Right.ExpiresAt
            && Left.PriorityScore == Right.PriorityScore;
    }
}

FTSEventScenarioRunner::FTSEventScenarioRunner(
    FTSScenarioDefinition InScenario,
    const ITSFlowScenarioProvider& InProvider,
    FTSScenarioReporter& InReporter
)
    : Scenario(ValidateAndOrderScenario(
          std::move(InScenario),
          InProvider
      ))
    , Host(
          Scenario.CoreSettings,
          Clock.MakeProvider(),
          Scenario.PipelineSettings
      )
    , Provider(InProvider)
    , Reporter(InReporter)
{
}

FTSScenarioDefinition FTSEventScenarioRunner::ValidateAndOrderScenario(
    FTSScenarioDefinition InScenario,
    const ITSFlowScenarioProvider& InProvider
)
{
    if (!IsValidFlow(InProvider.GetObservedFlow()))
    {
        throw std::invalid_argument("Scenario provider flow is invalid");
    }

    if (InScenario.RuntimeSettings.ProcessingDuration.count() <= 0)
    {
        throw std::invalid_argument(
            "Scenario processing duration must be positive"
        );
    }

    if (InScenario.RuntimeSettings.DefaultArrivalInterval.count() < 0)
    {
        throw std::invalid_argument(
            "Scenario default arrival interval cannot be negative"
        );
    }

    if (InScenario.Inputs.empty())
    {
        throw std::invalid_argument(
            "Scenario must contain at least one scheduled input"
        );
    }

    std::unordered_set<std::uint64_t> Sequences;
    Sequences.reserve(InScenario.Inputs.size());
    std::chrono::milliseconds LastArrivalOffset{0};
    for (const FTSScheduledScenarioInput& Input : InScenario.Inputs)
    {
        if (Input.Sequence == 0)
        {
            throw std::invalid_argument(
                "Scheduled input sequence cannot be zero"
            );
        }

        if (!Sequences.insert(Input.Sequence).second)
        {
            throw std::invalid_argument(
                "Scheduled input sequences must be unique"
            );
        }

        if (Input.ArrivalOffset.count() < 0)
        {
            throw std::invalid_argument(
                "Scheduled input offset cannot be negative"
            );
        }

        if (!Input.PostAction)
        {
            throw std::invalid_argument(
                "Scheduled input has no Host post action"
            );
        }

        if (Input.ArrivalOffset > LastArrivalOffset)
        {
            LastArrivalOffset = Input.ArrivalOffset;
        }
    }

    std::sort(
        InScenario.Inputs.begin(),
        InScenario.Inputs.end(),
        [](const FTSScheduledScenarioInput& Left,
           const FTSScheduledScenarioInput& Right)
        {
            if (Left.ArrivalOffset != Right.ArrivalOffset)
            {
                return Left.ArrivalOffset < Right.ArrivalOffset;
            }
            return Left.Sequence < Right.Sequence;
        }
    );

    if (InScenario.Inputs.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    {
        throw std::overflow_error("Scenario contains too many inputs");
    }

    const std::int64_t InputCount =
        static_cast<std::int64_t>(InScenario.Inputs.size());
    const std::int64_t ProcessingMilliseconds =
        InScenario.RuntimeSettings.ProcessingDuration.count();
    constexpr std::int64_t Maximum =
        std::numeric_limits<std::int64_t>::max();
    if (ProcessingMilliseconds > Maximum / InputCount)
    {
        throw std::overflow_error(
            "Scenario processing timeline exceeds milliseconds range"
        );
    }

    const std::chrono::milliseconds MaximumProcessingSpan{
        ProcessingMilliseconds * InputCount
    };
    const std::chrono::milliseconds MaximumScenarioOffset = AddOffsets(
        LastArrivalOffset,
        MaximumProcessingSpan
    );
    FTSScenarioClock ValidationClock;
    ValidationClock.AdvanceTo(MaximumScenarioOffset);

    return InScenario;
}

std::chrono::milliseconds FTSEventScenarioRunner::AddOffsets(
    std::chrono::milliseconds Left,
    std::chrono::milliseconds Right
)
{
    if (Left.count() < 0 || Right.count() < 0)
    {
        throw std::invalid_argument("Scenario offsets cannot be negative");
    }

    constexpr std::int64_t Maximum = std::numeric_limits<std::int64_t>::max();
    if (Left.count() > Maximum - Right.count())
    {
        throw std::overflow_error("Scenario offset overflow");
    }

    return std::chrono::milliseconds{Left.count() + Right.count()};
}

std::optional<std::chrono::milliseconds>
FTSEventScenarioRunner::GetNextEventOffset() const
{
    std::optional<std::chrono::milliseconds> Next;
    const auto Consider = [&](std::chrono::milliseconds Candidate)
    {
        if (!Next.has_value() || Candidate < *Next)
        {
            Next = Candidate;
        }
    };

    if (NextInputIndex < Scenario.Inputs.size())
    {
        Consider(Scenario.Inputs[NextInputIndex].ArrivalOffset);
    }

    if (ActiveProcessing.has_value())
    {
        Consider(ActiveProcessing->CompletionOffset);
    }

    if (NextWakeOffset.has_value())
    {
        Consider(*NextWakeOffset);
    }

    if (bMoreCommandsPending)
    {
        Consider(Clock.GetOffset());
    }

    return Next;
}

std::chrono::milliseconds FTSEventScenarioRunner::ToScenarioOffset(
    FTSEventQueueTimePoint TimePoint
) const
{
    if (TimePoint == FTSEventQueueTimePoint::max())
    {
        throw std::logic_error(
            "Host returned the no-expiration sentinel as a wake time"
        );
    }

    if (TimePoint < Clock.GetOrigin())
    {
        throw std::logic_error("Host wake time precedes scenario origin");
    }

    return std::chrono::ceil<std::chrono::milliseconds>(
        TimePoint - Clock.GetOrigin()
    );
}

void FTSEventScenarioRunner::UpdateNextWake(
    const FTSNextWakeTimeResult& NextWake
)
{
    switch (NextWake.Status)
    {
    case ETSNextWakeStatus::NoWakeScheduled:
        NextWakeOffset.reset();
        return;

    case ETSNextWakeStatus::WakeScheduled:
        NextWakeOffset = ToScenarioOffset(NextWake.WakeTime);
        return;
    }

    throw std::logic_error("Host returned an unknown next-wake status");
}

std::size_t FTSEventScenarioRunner::CountLifecycleEvents(
    const FTSEventHostCycleResult& Result
) noexcept
{
    std::size_t Count = Result.DueExpirations.LifecycleEvents.size();

    if (Result.AdmissionResult.has_value() &&
        Result.AdmissionResult->EnqueueResult.has_value())
    {
        Count += Result.AdmissionResult
            ->EnqueueResult
            ->LifecycleEvents
            .size();
    }

    if (Result.CompletionResult.has_value())
    {
        if (Result.CompletionResult->ConfirmResult.has_value())
        {
            Count += Result.CompletionResult
                ->ConfirmResult
                ->LifecycleEvents
                .size();
        }
        if (Result.CompletionResult->CancelResult.has_value())
        {
            Count += Result.CompletionResult
                ->CancelResult
                ->LifecycleEvents
                .size();
        }
    }

    if (Result.PumpResult.has_value())
    {
        Count += Result.PumpResult->LifecycleEvents.size();
    }

    return Count;
}

void FTSEventScenarioRunner::ProcessCycleResult(
    const FTSEventHostCycleResult& Result
)
{
    Reporter.ReportCycle(Clock.GetOffset(), Result, Clock.GetOrigin());

    if (Result.CompletionResult.has_value())
    {
        const FTSProcessingCompletionResult& Completion =
            *Result.CompletionResult;
        if (!ExpectedCompletionEmissionId.has_value() ||
            Completion.EmissionId != *ExpectedCompletionEmissionId)
        {
            throw std::logic_error(
                "Host completion does not match the scheduled processing"
            );
        }

        if (Completion.ProcessingResult != ETSProcessingResult::Succeeded ||
            !Completion.ConfirmResult.has_value() ||
            Completion.ConfirmResult->Status != ETSConfirmStatus::Confirmed ||
            Completion.CancelResult.has_value())
        {
            throw std::logic_error(
                "Scheduled Succeeded completion was not confirmed"
            );
        }

        ExpectedCompletionEmissionId.reset();
    }

    bLastCycleProducedDispatch = Result.Dispatch.has_value();
    if (Result.Dispatch.has_value())
    {
        if (ActiveProcessing.has_value())
        {
            throw std::logic_error(
                "Host dispatched while another processing is active"
            );
        }

        const FTSEmissionEnvelope& ActualEnvelope =
            GetDispatchEnvelope(*Result.Dispatch);

        // ObserveDispatch debe copiar todo lo necesario; Result y su variant mueren al
        // terminar el ciclo y no pueden convertirse en almacenamiento del provider.
        FTSObservedScenarioDispatch Observed =
            Provider.ObserveDispatch(*Result.Dispatch);
        if (!EnvelopesMatch(ActualEnvelope, Observed.Emission) ||
            Observed.Emission.Flow != Provider.GetObservedFlow() ||
            Observed.Emission.EmissionId == 0 ||
            !Observed.PostSucceededCompletion)
        {
            throw std::logic_error(
                "Scenario provider returned an invalid observed dispatch"
            );
        }

        Reporter.ReportDispatch(
            Clock.GetOffset(),
            Observed,
            Clock.GetOrigin()
        );

        const std::chrono::milliseconds CompletionOffset = AddOffsets(
            Clock.GetOffset(),
            Scenario.RuntimeSettings.ProcessingDuration
        );
        Reporter.ReportCompletionScheduled(
            Observed.Emission.EmissionId,
            CompletionOffset
        );

        ActiveProcessing.emplace(FTSActiveScenarioProcessing{
            Observed.Emission.EmissionId,
            CompletionOffset,
            std::move(Observed.PostSucceededCompletion)
        });
    }

    UpdateNextWake(Result.NextWakeTime);
    bMoreCommandsPending = Result.bMoreCommandsPending;
}

void FTSEventScenarioRunner::RunHostCycles()
{
    do
    {
        const FTSEventHostCycleResult Result = Host.RunOneCycle();
        ProcessCycleResult(Result);
    }
    while (bMoreCommandsPending);
}

void FTSEventScenarioRunner::ExecuteDueCompletion()
{
    if (!ActiveProcessing.has_value() ||
        ActiveProcessing->CompletionOffset != Clock.GetOffset())
    {
        return;
    }

    FTSActiveScenarioProcessing Completing = std::move(*ActiveProcessing);
    ActiveProcessing.reset();
    ExpectedCompletionEmissionId = Completing.EmissionId;
    Completing.PostSucceededCompletion(Host);
    RunHostCycles();

    if (ExpectedCompletionEmissionId.has_value())
    {
        throw std::logic_error(
            "Scheduled completion command was not processed"
        );
    }
}

void FTSEventScenarioRunner::ExecuteInputsAtCurrentOffset()
{
    while (NextInputIndex < Scenario.Inputs.size() &&
        Scenario.Inputs[NextInputIndex].ArrivalOffset == Clock.GetOffset())
    {
        const FTSScheduledScenarioInput& Input =
            Scenario.Inputs[NextInputIndex];
        Reporter.ReportInputPublished(Clock.GetOffset(), Input);
        Input.PostAction(Host);
        ++NextInputIndex;
        RunHostCycles();
    }
}

void FTSEventScenarioRunner::RunMaintenanceCycle()
{
    const std::optional<std::chrono::milliseconds> PreviousWake =
        NextWakeOffset;
    const FTSEventHostCycleResult Result = Host.RunOneCycle();
    ProcessCycleResult(Result);

    const bool bProducedProgress =
        Result.ProcessedCommand != ETSEventHostCommandKind::None ||
        Result.Dispatch.has_value() ||
        CountLifecycleEvents(Result) != 0 ||
        NextWakeOffset != PreviousWake;

    if (!bProducedProgress &&
        NextWakeOffset.has_value() &&
        *NextWakeOffset <= Clock.GetOffset() &&
        NextWakeOffset == PreviousWake)
    {
        if (RepeatedImmediateWake == NextWakeOffset)
        {
            throw std::logic_error(
                "Host repeated an immediate wake without observable progress"
            );
        }
        RepeatedImmediateWake = NextWakeOffset;
    }
    else
    {
        RepeatedImmediateWake.reset();
    }
}

void FTSEventScenarioRunner::Run()
{
    while (true)
    {
        const std::optional<std::chrono::milliseconds> NextOffset =
            GetNextEventOffset();
        if (!NextOffset.has_value())
        {
            if (NextInputIndex != Scenario.Inputs.size() ||
                ActiveProcessing.has_value() ||
                NextWakeOffset.has_value() ||
                bMoreCommandsPending ||
                bLastCycleProducedDispatch)
            {
                throw std::logic_error(
                    "Scenario reached an inconsistent quiescent state"
                );
            }
            break;
        }

        if (*NextOffset < Clock.GetOffset())
        {
            throw std::logic_error(
                "Scenario selected an event before the current clock"
            );
        }

        if (*NextOffset > Clock.GetOffset())
        {
            Clock.AdvanceTo(*NextOffset);
            RepeatedImmediateWake.reset();
        }

        // En un empate se completa primero el InFlight, luego se publican inputs por
        // Sequence; el mantenimiento aislado sólo ocurre si no hubo comando.
        const bool bCompletionDue =
            ActiveProcessing.has_value() &&
            ActiveProcessing->CompletionOffset == Clock.GetOffset();
        if (bCompletionDue)
        {
            ExecuteDueCompletion();
        }

        const bool bInputsDue =
            NextInputIndex < Scenario.Inputs.size() &&
            Scenario.Inputs[NextInputIndex].ArrivalOffset == Clock.GetOffset();
        if (bInputsDue)
        {
            ExecuteInputsAtCurrentOffset();
        }

        if (!bCompletionDue && !bInputsDue &&
            NextWakeOffset.has_value() &&
            *NextWakeOffset <= Clock.GetOffset())
        {
            RunMaintenanceCycle();
        }
    }

    Reporter.ReportSummary();
}
