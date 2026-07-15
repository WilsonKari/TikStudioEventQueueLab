#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class TikStudioEventQueueSystem::FImpl final
{
public:
    struct FEmissionIdentity
    {
        FTSEmissionId EmissionId = 0;
        FTSEmissionSequence Sequence = 0;
    };

    enum class EAdmissionValidationStatus : std::uint8_t
    {
        Valid,
        InvalidFlow,
        Disabled,
        InvalidTTL
    };

    struct FAdmissionValidationResult
    {
        EAdmissionValidationStatus Status =
            EAdmissionValidationStatus::InvalidFlow;
        const FTSFlowQueueSettings* FlowSettings = nullptr;
        std::chrono::milliseconds EffectiveTTL{0};
    };

    enum class EInternalEmissionState : std::uint8_t
    {
        Pending,
        InFlight
    };

    // Unidad autoritativa de una emisión viva. Conserva los snapshots efectivos
    // necesarios para decidir su ciclo de vida sin volver a consultar settings.
    struct FInternalEmissionRecord
    {
        FTSEmissionEnvelope Envelope{};
        EInternalEmissionState State = EInternalEmissionState::Pending;
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard;
        bool bProtectedFromEviction = false;
        std::uint64_t Revision = 1;
    };

    // Snapshot derivado y reconstruible. EmissionId + Revision permite rechazar
    // una clave obsoleta sin almacenar enlaces hacia el record autoritativo.
    struct FPriorityIndexEntry
    {
        std::int64_t PriorityScore = 0;
        FTSEmissionSequence Sequence = 0;
        FTSEmissionId EmissionId = 0;
        std::uint64_t Revision = 0;
    };

    // std::priority_queue deja arriba el mayor score; Sequence ascendente conserva
    // FIFO entre emisiones con la misma prioridad congelada.
    struct FPriorityIndexCompare
    {
        bool operator()(
            const FPriorityIndexEntry& Left,
            const FPriorityIndexEntry& Right
        ) const noexcept
        {
            if (Left.PriorityScore != Right.PriorityScore)
            {
                return Left.PriorityScore < Right.PriorityScore;
            }

            return Left.Sequence > Right.Sequence;
        }
    };

    // Sólo representa vencimientos finitos. La política permanece en Records para
    // que el índice temporal siga siendo una vista mínima y no autoritativa.
    struct FExpirationIndexEntry
    {
        FTSEventQueueTimePoint ExpiresAt{};
        FTSEmissionSequence Sequence = 0;
        FTSEmissionId EmissionId = 0;
        std::uint64_t Revision = 0;
    };

    // Invierte la prioridad natural para obtener un min-heap por vencimiento y usa
    // Sequence como desempate determinista FIFO.
    struct FExpirationIndexCompare
    {
        bool operator()(
            const FExpirationIndexEntry& Left,
            const FExpirationIndexEntry& Right
        ) const noexcept
        {
            if (Left.ExpiresAt != Right.ExpiresAt)
            {
                return Left.ExpiresAt > Right.ExpiresAt;
            }

            return Left.Sequence > Right.Sequence;
        }
    };

    explicit FImpl(
        FTSEventQueueSettings InSettings,
        FTSNowProvider InNowProvider
    )
        : Settings(std::move(InSettings))
        , NowProvider(std::move(InNowProvider))
    {
        if (!NowProvider)
        {
            NowProvider = []() noexcept
            {
                return FTSEventQueueClock::now();
            };
        }
    }

    // El par ID/Sequence avanza como una sola identidad monotónica. Cero marca
    // agotamiento permanente, por lo que nunca se reutilizan valores tras el máximo.
    [[nodiscard]]
    bool TryAllocateEmissionIdentity(
        FEmissionIdentity& OutIdentity
    ) noexcept
    {
        if (NextEmissionId == 0 || NextSequence == 0)
        {
            return false;
        }

        OutIdentity.EmissionId = NextEmissionId;
        OutIdentity.Sequence = NextSequence;

        AdvanceCounterOrMarkExhausted(NextEmissionId);
        AdvanceCounterOrMarkExhausted(NextSequence);

        return true;
    }

    // Fija la precedencia de rechazos y resuelve el TTL efectivo antes de cualquier
    // mutación, de modo que una solicitud inválida no consuma identidad ni tiempo.
    [[nodiscard]]
    FAdmissionValidationResult ValidateEnqueueRequest(
        const FTSEnqueueRequest& Request
    ) const noexcept
    {
        FAdmissionValidationResult Result;

        if (!IsValidFlow(Request.Flow))
        {
            return Result;
        }

        const FTSFlowQueueSettings* ResolvedFlowSettings =
            Settings.TryGetFlowSettings(Request.Flow);

        if (ResolvedFlowSettings == nullptr)
        {
            return Result;
        }

        Result.FlowSettings = ResolvedFlowSettings;
        Result.EffectiveTTL = Request.bOverrideTTL
            ? Request.TTLOverride
            : ResolvedFlowSettings->TTL;

        if (!ResolvedFlowSettings->bEnabled)
        {
            Result.Status = EAdmissionValidationStatus::Disabled;
            return Result;
        }

        if (Result.EffectiveTTL.count() < 0)
        {
            Result.Status = EAdmissionValidationStatus::InvalidTTL;
            return Result;
        }

        Result.Status = EAdmissionValidationStatus::Valid;
        return Result;
    }

    // La saturación mantiene un orden total incluso en los extremos sin incurrir en
    // overflow firmado al combinar el peso base y el ajuste externo.
    [[nodiscard]]
    static std::int64_t CalculatePriorityScore(
        std::int32_t BaseWeight,
        std::int64_t PriorityAdjustment
    ) noexcept
    {
        const std::int64_t BaseWeight64 =
            static_cast<std::int64_t>(BaseWeight);
        constexpr std::int64_t MinValue =
            std::numeric_limits<std::int64_t>::min();
        constexpr std::int64_t MaxValue =
            std::numeric_limits<std::int64_t>::max();

        if (BaseWeight64 > 0 && PriorityAdjustment > MaxValue - BaseWeight64)
        {
            return MaxValue;
        }

        if (BaseWeight64 < 0 && PriorityAdjustment < MinValue - BaseWeight64)
        {
            return MinValue;
        }

        return PriorityAdjustment + BaseWeight64;
    }

    // Aísla el proveedor para que cada operación pública sensible al tiempo pueda
    // trabajar con una única instantánea coherente de Now.
    [[nodiscard]]
    FTSEventQueueTimePoint CaptureNow() const
    {
        return NowProvider();
    }

    // Records define qué emisiones siguen vivas; este escaneo O(n) evita introducir
    // una segunda fuente de verdad mientras no existan contadores derivados.
    [[nodiscard]]
    bool IsFlowAtCapacity(
        ETSEventFlow Flow,
        std::uint32_t MaxSlots
    ) const noexcept
    {
        if (MaxSlots == 0)
        {
            return true;
        }

        std::size_t LiveCount = 0;

        for (const auto& Entry : Records)
        {
            if (Entry.second.Envelope.Flow != Flow)
            {
                continue;
            }

            ++LiveCount;

            if (LiveCount >= static_cast<std::size_t>(MaxSlots))
            {
                return true;
            }
        }

        return false;
    }

    // time_point::max() es el sentinel de “sin expiración”. La comparación amplia
    // evita casts fuera de rango y ceil impide truncar un TTL positivo a cero ticks.
    [[nodiscard]]
    static FTSEventQueueTimePoint CalculateExpiresAt(
        FTSEventQueueTimePoint Now,
        std::chrono::milliseconds EffectiveTTL
    ) noexcept
    {
        using FClockDuration = FTSEventQueueTimePoint::duration;
        using FWideDuration = std::common_type_t<
            std::chrono::duration<long double, FClockDuration::period>,
            std::chrono::duration<long double, std::milli>
        >;

        const FTSEventQueueTimePoint MaxTimePoint =
            FTSEventQueueTimePoint::max();

        if (EffectiveTTL.count() == 0)
        {
            return MaxTimePoint;
        }

        const FWideDuration WideTTL = EffectiveTTL;
        const FWideDuration WideMaxClockDuration = FClockDuration::max();

        if (WideTTL > WideMaxClockDuration)
        {
            return MaxTimePoint;
        }

        const FClockDuration TTLDuration =
            std::chrono::ceil<FClockDuration>(EffectiveTTL);
        const FClockDuration NowDuration = Now.time_since_epoch();

        if (
            NowDuration > FClockDuration::zero()
            && TTLDuration > FClockDuration::max() - NowDuration
        )
        {
            return MaxTimePoint;
        }

        return FTSEventQueueTimePoint{NowDuration + TTLDuration};
    }

    // Una clave temporal sólo es vigente si identidad, revisión, estado y snapshots
    // de orden siguen coincidiendo con la fuente autoritativa.
    [[nodiscard]]
    bool IsExpirationEntryCurrent(
        const FExpirationIndexEntry& Entry
    ) const
    {
        const auto RecordIt = Records.find(Entry.EmissionId);

        if (RecordIt == Records.end())
        {
            return false;
        }

        const FInternalEmissionRecord& Record = RecordIt->second;

        return Record.State == EInternalEmissionState::Pending
            && Record.Revision == Entry.Revision
            && Record.Envelope.Sequence == Entry.Sequence
            && Record.Envelope.ExpiresAt == Entry.ExpiresAt
            && Record.Envelope.ExpiresAt != FTSEventQueueTimePoint::max();
    }

    // La invalidación lazy descarta únicamente el frente porque las entradas más
    // profundas todavía no pueden determinar el próximo despertar o vencimiento.
    void DiscardStaleExpirationEntries()
    {
        while (!ExpirationIndex.empty())
        {
            if (IsExpirationEntryCurrent(ExpirationIndex.top()))
            {
                return;
            }

            ExpirationIndex.pop();
        }
    }

    // La vista de prioridad es vigente sólo mientras el record continúe siendo un
    // candidato Pending con los mismos snapshots usados para ordenar.
    [[nodiscard]]
    bool IsPriorityEntryCurrent(
        const FPriorityIndexEntry& Entry
    ) const
    {
        const auto RecordIt = Records.find(Entry.EmissionId);

        if (RecordIt == Records.end())
        {
            return false;
        }

        const FInternalEmissionRecord& Record = RecordIt->second;

        return Record.State == EInternalEmissionState::Pending
            && Record.Revision == Entry.Revision
            && Record.Envelope.Sequence == Entry.Sequence
            && Record.Envelope.PriorityScore == Entry.PriorityScore;
    }

    // Sólo el frente puede afectar la próxima selección; las claves stale se
    // descartan sin tocar Records ni producir eventos de ciclo de vida.
    void DiscardStalePriorityEntries()
    {
        while (!PriorityIndex.empty())
        {
            if (IsPriorityEntryCurrent(PriorityIndex.top()))
            {
                return;
            }

            PriorityIndex.pop();
        }
    }

    // El motivo terminal deriva de la política congelada al admitir; un valor fuera
    // del contrato revela una invariante rota antes de eliminar estado autoritativo.
    [[nodiscard]]
    static ETSEmissionTerminalReason ResolveExpirationTerminalReason(
        ETSEventExpirePolicy ExpirePolicy
    )
    {
        switch (ExpirePolicy)
        {
        case ETSEventExpirePolicy::Discard:
            return ETSEmissionTerminalReason::ExpiredDiscard;

        case ETSEventExpirePolicy::Consolidate:
            return ETSEmissionTerminalReason::ExpiredConsolidate;
        }

        throw std::logic_error("Invalid expiration policy");
    }

    // Recibe un Now ya capturado para compartir una única semántica de expiración
    // entre operaciones públicas sin duplicar tiempo, orden ni transiciones.
    void ProcessDueExpirationsAt(
        FTSEventQueueTimePoint Now,
        FTSEmissionLifecycleEvents& OutLifecycleEvents
    )
    {
        while (true)
        {
            DiscardStaleExpirationEntries();

            if (ExpirationIndex.empty())
            {
                break;
            }

            const FExpirationIndexEntry Entry = ExpirationIndex.top();

            if (!(Entry.ExpiresAt <= Now))
            {
                break;
            }

            const auto RecordIt = Records.find(Entry.EmissionId);
            const FInternalEmissionRecord& Record = RecordIt->second;

            FTSEmissionLifecycleEvent LifecycleEvent;
            LifecycleEvent.Envelope = Record.Envelope;
            LifecycleEvent.Reason = ResolveExpirationTerminalReason(
                Record.ExpirePolicy
            );

            // Publicar primero mantiene record y clave temporal disponibles si el
            // vector lanza; la entrada de prioridad queda stale tras la eliminación.
            OutLifecycleEvents.push_back(std::move(LifecycleEvent));
            ExpirationIndex.pop();
            Records.erase(RecordIt);
        }
    }

    FTSEventQueueSettings Settings;
    FTSNowProvider NowProvider;
    FTSEmissionId NextEmissionId = 1;
    FTSEmissionSequence NextSequence = 1;
    // Cero representa idle. Un valor activo sólo localiza el record autoritativo;
    // no duplica estado, envelope ni ownership fuera de Records.
    FTSEmissionId InFlightEmissionId = 0;
    // Única fuente de verdad. Los heaps sólo contienen vistas pequeñas que pueden
    // quedar stale y reconstruirse a partir de estos records.
    std::unordered_map<FTSEmissionId, FInternalEmissionRecord> Records;
    std::priority_queue<
        FPriorityIndexEntry,
        std::vector<FPriorityIndexEntry>,
        FPriorityIndexCompare
    > PriorityIndex;
    std::priority_queue<
        FExpirationIndexEntry,
        std::vector<FExpirationIndexEntry>,
        FExpirationIndexCompare
    > ExpirationIndex;

private:
    static void AdvanceCounterOrMarkExhausted(
        std::uint64_t& Counter
    ) noexcept
    {
        if (Counter == std::numeric_limits<std::uint64_t>::max())
        {
            Counter = 0;
            return;
        }

        ++Counter;
    }
};

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    FTSEventQueueSettings Settings,
    FTSNowProvider NowProvider
)
    : Impl(std::make_unique<FImpl>(
        std::move(Settings),
        std::move(NowProvider)
    ))
{
}

FTSEnqueueResult TikStudioEventQueueSystem::Enqueue(
    const FTSEnqueueRequest& Request
)
{
    // La admisión valida antes de mutar y congela todos los valores de orden con una
    // sola captura temporal antes de establecer el record autoritativo.
    FTSEnqueueResult Result;
    const FImpl::FAdmissionValidationResult Validation =
        Impl->ValidateEnqueueRequest(Request);

    switch (Validation.Status)
    {
    case FImpl::EAdmissionValidationStatus::InvalidFlow:
        Result.Status = ETSEnqueueStatus::RejectedInvalidFlow;
        return Result;

    case FImpl::EAdmissionValidationStatus::Disabled:
        Result.Status = ETSEnqueueStatus::RejectedDisabled;
        return Result;

    case FImpl::EAdmissionValidationStatus::InvalidTTL:
        Result.Status = ETSEnqueueStatus::RejectedInvalidTTL;
        return Result;

    case FImpl::EAdmissionValidationStatus::Valid:
        break;
    }

    const FTSFlowQueueSettings& FlowSettings = *Validation.FlowSettings;

    if (Impl->IsFlowAtCapacity(Request.Flow, FlowSettings.MaxSlots))
    {
        Result.Status = ETSEnqueueStatus::RejectedAtCapacity;
        return Result;
    }

    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    const std::int64_t PriorityScore = FImpl::CalculatePriorityScore(
        FlowSettings.BaseWeight,
        Request.PriorityAdjustment
    );
    const FTSEventQueueTimePoint ExpiresAt = FImpl::CalculateExpiresAt(
        Now,
        Validation.EffectiveTTL
    );

    FImpl::FEmissionIdentity Identity;

    if (!Impl->TryAllocateEmissionIdentity(Identity))
    {
        Result.Status = ETSEnqueueStatus::RejectedIdentityExhausted;
        return Result;
    }

    FTSEmissionEnvelope Envelope;
    Envelope.EmissionId = Identity.EmissionId;
    Envelope.Flow = Request.Flow;
    Envelope.Sequence = Identity.Sequence;
    Envelope.CreatedAt = Now;
    Envelope.ExpiresAt = ExpiresAt;
    Envelope.PriorityScore = PriorityScore;

    FImpl::FInternalEmissionRecord Record;
    Record.Envelope = Envelope;
    Record.State = FImpl::EInternalEmissionState::Pending;
    Record.ExpirePolicy = FlowSettings.ExpirePolicy;
    Record.bProtectedFromEviction =
        Request.bProtectedFromEviction
        || FlowSettings.bExemptFromEviction;
    Record.Revision = 1;

    // Una colisión contradice la identidad monotónica: es una violación interna, no
    // una condición de rechazo que deba exponerse en el contrato público.
    const auto [RecordIt, bInserted] = Impl->Records.emplace(
        Envelope.EmissionId,
        std::move(Record)
    );

    if (!bInserted)
    {
        throw std::logic_error("Duplicate emission identity");
    }

    // Accepted exige que todas las vistas derivadas requeridas existan. Si indexar
    // falla, retirar el record evita dejar estado autoritativo parcialmente admitido.
    try
    {
        const FImpl::FInternalEmissionRecord& StoredRecord = RecordIt->second;
        const FTSEmissionEnvelope& StoredEnvelope = StoredRecord.Envelope;

        Impl->PriorityIndex.push(FImpl::FPriorityIndexEntry{
            StoredEnvelope.PriorityScore,
            StoredEnvelope.Sequence,
            StoredEnvelope.EmissionId,
            StoredRecord.Revision
        });

        if (StoredEnvelope.ExpiresAt != FTSEventQueueTimePoint::max())
        {
            Impl->ExpirationIndex.push(FImpl::FExpirationIndexEntry{
                StoredEnvelope.ExpiresAt,
                StoredEnvelope.Sequence,
                StoredEnvelope.EmissionId,
                StoredRecord.Revision
            });
        }
    }
    catch (...)
    {
        Impl->Records.erase(RecordIt);
        throw;
    }

    Result.Status = ETSEnqueueStatus::Accepted;
    Result.AdmittedEmission = Envelope;
    return Result;
}

FTSPumpResult TikStudioEventQueueSystem::Pump()
{
    // Expirar primero garantiza que Busy, QueueEmpty y la selección observen el mismo
    // estado autoritativo para la única instantánea temporal de esta llamada.
    FTSPumpResult Result;
    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    Impl->ProcessDueExpirationsAt(Now, Result.LifecycleEvents);

    if (Impl->InFlightEmissionId != 0)
    {
        const auto InFlightRecordIt = Impl->Records.find(
            Impl->InFlightEmissionId
        );

        if (
            InFlightRecordIt == Impl->Records.end()
            || InFlightRecordIt->second.State
                != FImpl::EInternalEmissionState::InFlight
        )
        {
            throw std::logic_error("Invalid in-flight emission state");
        }

        Result.Outcome.Status = ETSPumpStatus::Busy;
        return Result;
    }

    Impl->DiscardStalePriorityEntries();

    if (Impl->PriorityIndex.empty())
    {
        Result.Outcome.Status = ETSPumpStatus::QueueEmpty;
        return Result;
    }

    const FImpl::FPriorityIndexEntry Entry = Impl->PriorityIndex.top();
    const auto RecordIt = Impl->Records.find(Entry.EmissionId);
    FImpl::FInternalEmissionRecord& Record = RecordIt->second;

    Result.Outcome.ReadyEmission = Record.Envelope;

    // El record permanece en Records y conserva el slot; el ID activo sólo impide
    // una segunda selección mientras su estado autoritativo sea InFlight.
    Record.State = FImpl::EInternalEmissionState::InFlight;
    Impl->InFlightEmissionId = Record.Envelope.EmissionId;
    Impl->PriorityIndex.pop();

    // La clave temporal no se toca: el cambio de estado basta para volverla stale.
    Result.Outcome.Status = ETSPumpStatus::EmissionReady;
    return Result;
}

// Consulta pura respecto del tiempo y de Records: sólo limpia claves stale del frente
// y expone el menor vencimiento vigente, aunque ya haya quedado en el pasado.
FTSNextWakeTimeResult TikStudioEventQueueSystem::GetNextWakeTime()
{
    FTSNextWakeTimeResult Result;

    Impl->DiscardStaleExpirationEntries();

    if (Impl->ExpirationIndex.empty())
    {
        return Result;
    }

    Result.Status = ETSNextWakeStatus::WakeScheduled;
    Result.WakeTime = Impl->ExpirationIndex.top().ExpiresAt;
    return Result;
}

FTSProcessDueExpirationsResult
TikStudioEventQueueSystem::ProcessDueExpirations()
{
    // Una sola captura hace determinista todo el lote; el min-heap preserva orden por
    // ExpiresAt y luego Sequence durante la generación de terminales.
    FTSProcessDueExpirationsResult Result;
    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    Impl->ProcessDueExpirationsAt(Now, Result.LifecycleEvents);

    return Result;
}

TikStudioEventQueueSystem::~TikStudioEventQueueSystem() = default;

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    TikStudioEventQueueSystem&&
) noexcept = default;

TikStudioEventQueueSystem& TikStudioEventQueueSystem::operator=(
    TikStudioEventQueueSystem&&
) noexcept = default;
