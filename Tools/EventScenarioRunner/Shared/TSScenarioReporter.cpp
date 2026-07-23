#include "Shared/TSScenarioReporter.h"

#include "Shared/TSFlowScenarioProvider.h"

#include <chrono>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    [[nodiscard]]
    std::string_view ToText(ETSEventFlow Value) noexcept
    {
        switch (Value)
        {
        case ETSEventFlow::Chat: return "Chat";
        case ETSEventFlow::Gift: return "Gift";
        case ETSEventFlow::GiftCombo: return "GiftCombo";
        case ETSEventFlow::Follow: return "Follow";
        case ETSEventFlow::Like: return "Like";
        case ETSEventFlow::LikeMilestone: return "LikeMilestone";
        case ETSEventFlow::Member: return "Member";
        case ETSEventFlow::MemberRate: return "MemberRate";
        case ETSEventFlow::RoomUser: return "RoomUser";
        case ETSEventFlow::RoomUserMilestone: return "RoomUserMilestone";
        case ETSEventFlow::RoomUserTop1Change: return "RoomUserTop1Change";
        case ETSEventFlow::Share: return "Share";
        case ETSEventFlow::ShareMilestone: return "ShareMilestone";
        case ETSEventFlow::Count: return "Count";
        }
        return "UnknownFlow";
    }

    [[nodiscard]]
    std::string_view ToText(ETSEventHostCommandKind Value) noexcept
    {
        switch (Value)
        {
        case ETSEventHostCommandKind::None: return "None";
        case ETSEventHostCommandKind::ChatInput: return "ChatInput";
        case ETSEventHostCommandKind::FollowInput: return "FollowInput";
        case ETSEventHostCommandKind::ChatCompletion: return "ChatCompletion";
        case ETSEventHostCommandKind::FollowCompletion: return "FollowCompletion";
        case ETSEventHostCommandKind::ShareInput: return "ShareInput";
        case ETSEventHostCommandKind::ShareCompletion: return "ShareCompletion";
        case ETSEventHostCommandKind::LikeInput: return "LikeInput";
        case ETSEventHostCommandKind::LikeCompletion: return "LikeCompletion";
        case ETSEventHostCommandKind::RoomUserInput: return "RoomUserInput";
        case ETSEventHostCommandKind::RoomUserCompletion:
            return "RoomUserCompletion";
        case ETSEventHostCommandKind::GiftInput: return "GiftInput";
        case ETSEventHostCommandKind::GiftCompletion: return "GiftCompletion";
        case ETSEventHostCommandKind::MemberInput: return "MemberInput";
        case ETSEventHostCommandKind::MemberCompletion: return "MemberCompletion";
        case ETSEventHostCommandKind::FlowSettingsUpdate:
            return "FlowSettingsUpdate";
        }
        return "UnknownHostCommand";
    }

    [[nodiscard]]
    std::string_view ToText(ETSPipelineAdmissionStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSPipelineAdmissionStatus::NoEmission: return "NoEmission";
        case ETSPipelineAdmissionStatus::RejectedInvalidInput:
            return "RejectedInvalidInput";
        case ETSPipelineAdmissionStatus::RejectedSemanticLimit:
            return "RejectedSemanticLimit";
        case ETSPipelineAdmissionStatus::Accumulated: return "Accumulated";
        case ETSPipelineAdmissionStatus::RejectedPayloadIdentityExhausted:
            return "RejectedPayloadIdentityExhausted";
        case ETSPipelineAdmissionStatus::RejectedByCore:
            return "RejectedByCore";
        case ETSPipelineAdmissionStatus::Accepted: return "Accepted";
        }
        return "UnknownAdmissionStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSEnqueueStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSEnqueueStatus::Accepted: return "Accepted";
        case ETSEnqueueStatus::AcceptedWithEviction:
            return "AcceptedWithEviction";
        case ETSEnqueueStatus::RejectedInvalidFlow:
            return "RejectedInvalidFlow";
        case ETSEnqueueStatus::RejectedDisabled: return "RejectedDisabled";
        case ETSEnqueueStatus::RejectedInvalidTTL:
            return "RejectedInvalidTTL";
        case ETSEnqueueStatus::RejectedIdentityExhausted:
            return "RejectedIdentityExhausted";
        case ETSEnqueueStatus::RejectedAtCapacity:
            return "RejectedAtCapacity";
        }
        return "UnknownEnqueueStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSPumpStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSPumpStatus::NotRequested: return "NotRequested";
        case ETSPumpStatus::EmissionReady: return "EmissionReady";
        case ETSPumpStatus::QueueEmpty: return "QueueEmpty";
        case ETSPumpStatus::Busy: return "Busy";
        }
        return "UnknownPumpStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSEmissionTerminalReason Value) noexcept
    {
        switch (Value)
        {
        case ETSEmissionTerminalReason::Confirmed: return "Confirmed";
        case ETSEmissionTerminalReason::ExpiredDiscard:
            return "ExpiredDiscard";
        case ETSEmissionTerminalReason::ExpiredConsolidate:
            return "ExpiredConsolidate";
        case ETSEmissionTerminalReason::Evicted: return "Evicted";
        case ETSEmissionTerminalReason::Cancelled: return "Cancelled";
        }
        return "UnknownTerminalReason";
    }

    [[nodiscard]]
    std::string_view ToText(ETSProcessingResult Value) noexcept
    {
        switch (Value)
        {
        case ETSProcessingResult::Succeeded: return "Succeeded";
        case ETSProcessingResult::Cancelled: return "Cancelled";
        case ETSProcessingResult::Failed: return "Failed";
        }
        return "UnknownProcessingResult";
    }

    [[nodiscard]]
    std::string_view ToText(ETSNextWakeStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSNextWakeStatus::NoWakeScheduled: return "NoWakeScheduled";
        case ETSNextWakeStatus::WakeScheduled: return "WakeScheduled";
        }
        return "UnknownNextWakeStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSUpdateFlowSettingsStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSUpdateFlowSettingsStatus::Updated: return "Updated";
        case ETSUpdateFlowSettingsStatus::RejectedInvalidFlow:
            return "RejectedInvalidFlow";
        case ETSUpdateFlowSettingsStatus::RejectedInvalidTTL:
            return "RejectedInvalidTTL";
        case ETSUpdateFlowSettingsStatus::RejectedInvalidExpirePolicy:
            return "RejectedInvalidExpirePolicy";
        }
        return "UnknownFlowSettingsUpdateStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSConfirmStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSConfirmStatus::Confirmed: return "Confirmed";
        case ETSConfirmStatus::NoInFlightEmission:
            return "NoInFlightEmission";
        case ETSConfirmStatus::EmissionIdMismatch:
            return "EmissionIdMismatch";
        }
        return "UnknownConfirmStatus";
    }

    [[nodiscard]]
    std::string_view ToText(ETSCancelInFlightStatus Value) noexcept
    {
        switch (Value)
        {
        case ETSCancelInFlightStatus::Cancelled: return "Cancelled";
        case ETSCancelInFlightStatus::NoInFlightEmission:
            return "NoInFlightEmission";
        case ETSCancelInFlightStatus::EmissionIdMismatch:
            return "EmissionIdMismatch";
        }
        return "UnknownCancelStatus";
    }

    [[nodiscard]]
    std::string FormatTimePoint(
        FTSEventQueueTimePoint Value,
        FTSEventQueueTimePoint Origin
    )
    {
        // El sentinel no participa en aritmética para evitar overflow.
        if (Value == FTSEventQueueTimePoint::max())
        {
            return "sin expiración";
        }

        return std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Value - Origin
            ).count()
        ) + " ms";
    }

    void ReportEnvelope(
        std::ostream& Output,
        const FTSEmissionEnvelope& Envelope,
        FTSEventQueueTimePoint Origin
    )
    {
        Output
            << "    EmissionId=" << Envelope.EmissionId
            << ", Flow=" << ToText(Envelope.Flow)
            << ", PriorityScore=" << Envelope.PriorityScore
            << ", CreatedAt=" << FormatTimePoint(Envelope.CreatedAt, Origin)
            << ", ExpiresAt=" << FormatTimePoint(Envelope.ExpiresAt, Origin)
            << '\n';
    }
}

FTSScenarioReporter::FTSScenarioReporter(std::ostream& InOutput) noexcept
    : Output(InOutput)
{
}

void FTSScenarioReporter::ReportScenarioConfiguration(
    const ITSFlowScenarioProvider& Provider,
    const FTSScenarioDefinition& Scenario
)
{
    const ETSEventFlow Flow = Provider.GetObservedFlow();
    const FTSFlowQueueSettings* const FlowSettings =
        Scenario.CoreSettings.TryGetFlowSettings(Flow);
    if (FlowSettings == nullptr)
    {
        throw std::logic_error("Scenario flow has no Core settings");
    }

    Output << "\n=== Escenario configurado ===\n"
        << "Flow observado: " << Provider.GetDisplayName() << " ("
        << ToText(Flow) << ")\n"
        << "BaseWeight: " << FlowSettings->BaseWeight << '\n'
        << "TTL: " << FlowSettings->TTL.count() << " ms"
        << (FlowSettings->TTL.count() == 0 ? " (sin expiración)" : "")
        << '\n'
        << "MaxSlots: " << FlowSettings->MaxSlots
        << (FlowSettings->MaxSlots == 0 ? " (sin capacidad)" : "")
        << '\n'
        << "Duración de procesamiento: "
        << Scenario.RuntimeSettings.ProcessingDuration.count() << " ms\n"
        << "Intervalo predeterminado: "
        << Scenario.RuntimeSettings.DefaultArrivalInterval.count() << " ms\n";

    for (const std::string& Line : Scenario.ConfigurationDetailLines)
    {
        Output << Line << '\n';
    }

    Output << "Secuencia:\n";
    for (const FTSScheduledScenarioInput& Input : Scenario.Inputs)
    {
        Output << "  [" << Input.ArrivalOffset.count() << " ms, #"
            << Input.Sequence << "] " << Input.Description << '\n';
    }
}

void FTSScenarioReporter::ReportInputPublished(
    std::chrono::milliseconds Offset,
    const FTSScheduledScenarioInput& Input
)
{
    ++PublishedInputs;
    Output << "\n[" << Offset.count() << " ms] Publicado: "
        << Input.Description << '\n';
}

void FTSScenarioReporter::ReportLifecycleEvents(
    std::string_view Source,
    const FTSEmissionLifecycleEvents& Events,
    FTSEventQueueTimePoint Origin
)
{
    for (const FTSEmissionLifecycleEvent& Event : Events)
    {
        const std::string Reason{ToText(Event.Reason)};
        Output << "  Terminal (" << Source << "): " << Reason << '\n';
        ReportEnvelope(Output, Event.Envelope, Origin);
        if (Event.Reason == ETSEmissionTerminalReason::ExpiredDiscard ||
            Event.Reason == ETSEmissionTerminalReason::ExpiredConsolidate)
        {
            ++ExpirationsByReason[Reason];
        }
    }
}

void FTSScenarioReporter::ReportCycle(
    std::chrono::milliseconds Offset,
    const FTSEventHostCycleResult& Result,
    FTSEventQueueTimePoint Origin
)
{
    Output << "[" << Offset.count() << " ms] Ciclo Host: "
        << ToText(Result.ProcessedCommand) << '\n';

    ReportLifecycleEvents(
        "DueExpirations",
        Result.DueExpirations.LifecycleEvents,
        Origin
    );

    if (Result.AdmissionResult.has_value())
    {
        const FTSPipelineAdmissionResult& Admission = *Result.AdmissionResult;
        const std::string Status{ToText(Admission.Status)};
        Output << "  AdmissionStatus=" << Status
            << ", AffectedEmissionId=" << Admission.AffectedEmissionId
            << '\n';

        if (Admission.Status == ETSPipelineAdmissionStatus::Accepted)
        {
            ++AcceptedAdmissions;
        }
        else if (Admission.Status == ETSPipelineAdmissionStatus::Accumulated)
        {
            ++AccumulatedAdmissions;
        }
        else
        {
            ++RejectedAdmissions[Status];
        }

        if (Admission.EnqueueResult.has_value())
        {
            const FTSEnqueueResult& Enqueue = *Admission.EnqueueResult;
            Output << "  EnqueueStatus=" << ToText(Enqueue.Status)
                << ", AutoPumpStatus="
                << ToText(Enqueue.AutoPumpOutcome.Status) << '\n';
            if (Enqueue.Status == ETSEnqueueStatus::Accepted ||
                Enqueue.Status == ETSEnqueueStatus::AcceptedWithEviction)
            {
                ReportEnvelope(Output, Enqueue.AdmittedEmission, Origin);
            }
            ReportLifecycleEvents(
                "Enqueue",
                Enqueue.LifecycleEvents,
                Origin
            );
        }
    }

    if (Result.FlowSettingsUpdateResult.has_value())
    {
        Output << "  FlowSettingsUpdateStatus="
            << ToText(Result.FlowSettingsUpdateResult->Status)
            << ", Flow=" << ToText(Result.FlowSettingsUpdateResult->Flow)
            << '\n';
    }

    if (Result.CompletionResult.has_value())
    {
        const FTSProcessingCompletionResult& Completion =
            *Result.CompletionResult;
        Output << "  Completion: EmissionId=" << Completion.EmissionId
            << ", ProcessingResult=" << ToText(Completion.ProcessingResult)
            << '\n';

        if (Completion.ConfirmResult.has_value())
        {
            Output << "  ConfirmStatus="
                << ToText(Completion.ConfirmResult->Status)
                << ", AutoPumpStatus="
                << ToText(Completion.ConfirmResult->AutoPumpOutcome.Status)
                << '\n';
            if (Completion.ConfirmResult->Status == ETSConfirmStatus::Confirmed)
            {
                ++Confirmations;
            }
            ReportLifecycleEvents(
                "Completion/Confirm",
                Completion.ConfirmResult->LifecycleEvents,
                Origin
            );
        }

        if (Completion.CancelResult.has_value())
        {
            Output << "  CancelStatus="
                << ToText(Completion.CancelResult->Status) << '\n';
            ReportLifecycleEvents(
                "Completion/Cancel",
                Completion.CancelResult->LifecycleEvents,
                Origin
            );
        }
    }

    if (Result.PumpResult.has_value())
    {
        Output << "  PumpStatus="
            << ToText(Result.PumpResult->Outcome.Status) << '\n';
        ReportLifecycleEvents(
            "Pump",
            Result.PumpResult->LifecycleEvents,
            Origin
        );
    }

    Output << "  NextWakeTime=" << ToText(Result.NextWakeTime.Status);
    if (Result.NextWakeTime.Status == ETSNextWakeStatus::WakeScheduled)
    {
        Output << " ("
            << FormatTimePoint(Result.NextWakeTime.WakeTime, Origin)
            << ')';
    }
    Output << ", MoreCommands="
        << (Result.bMoreCommandsPending ? "sí" : "no") << '\n';
}

void FTSScenarioReporter::ReportDispatch(
    std::chrono::milliseconds Offset,
    const FTSObservedScenarioDispatch& Dispatch,
    FTSEventQueueTimePoint Origin
)
{
    ++Dispatches;
    ProcessingOrder.push_back(Dispatch.Emission.EmissionId);
    Output << "[" << Offset.count() << " ms] Dispatch\n";
    ReportEnvelope(Output, Dispatch.Emission, Origin);
    for (const std::string& Line : Dispatch.DetailLines)
    {
        Output << "    " << Line << '\n';
    }
}

void FTSScenarioReporter::ReportCompletionScheduled(
    FTSEmissionId EmissionId,
    std::chrono::milliseconds CompletionOffset
)
{
    Output << "  Completion Succeeded programado para EmissionId="
        << EmissionId << " en " << CompletionOffset.count() << " ms\n";
}

void FTSScenarioReporter::ReportSummary()
{
    Output << "\n=== Resumen ===\n"
        << "Entradas publicadas: " << PublishedInputs << '\n'
        << "Accepted: " << AcceptedAdmissions << '\n'
        << "Accumulated: " << AccumulatedAdmissions << '\n'
        << "Rechazos por estado:\n";

    if (RejectedAdmissions.empty())
    {
        Output << "  ninguno\n";
    }
    else
    {
        for (const auto& [Status, Count] : RejectedAdmissions)
        {
            Output << "  " << Status << ": " << Count << '\n';
        }
    }

    Output << "Dispatches: " << Dispatches << '\n'
        << "Confirmaciones: " << Confirmations << '\n'
        << "Expiraciones por razón:\n";
    if (ExpirationsByReason.empty())
    {
        Output << "  ninguna\n";
    }
    else
    {
        for (const auto& [Reason, Count] : ExpirationsByReason)
        {
            Output << "  " << Reason << ": " << Count << '\n';
        }
    }

    Output << "Orden de procesamiento:";
    if (ProcessingOrder.empty())
    {
        Output << " ninguno";
    }
    else
    {
        for (const FTSEmissionId EmissionId : ProcessingOrder)
        {
            Output << ' ' << EmissionId;
        }
    }
    Output << '\n';
}
