#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

static_assert(std::is_nothrow_move_constructible_v<FTSEnqueueResult>);
static_assert(std::is_nothrow_move_constructible_v<FTSPumpResult>);
static_assert(std::is_nothrow_move_constructible_v<FTSConfirmResult>);
static_assert(
    std::is_nothrow_move_constructible_v<FTSCancelInFlightResult>
);
static_assert(
    std::is_nothrow_move_constructible_v<FTSProcessDueExpirationsResult>
);

class TikStudioEventQueueSystem::FImpl final
{
public:
    struct FPreparedStateTag
    {
    };

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

    enum class EInFlightTargetStatus : std::uint8_t
    {
        Valid,
        NoInFlightEmission,
        EmissionIdMismatch
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

    // La copia preparada excluye al proveedor de tiempo: el Now se captura en la
    // instancia autoritativa y sólo el estado operativo alternativo llega al commit.
    FImpl(const FImpl& Source, FPreparedStateTag)
        : Settings(Source.Settings)
        , NextEmissionId(Source.NextEmissionId)
        , NextSequence(Source.NextSequence)
        , InFlightEmissionId(Source.InFlightEmissionId)
        , Records(Source.Records)
        , PriorityIndex(Source.PriorityIndex)
        , ExpirationIndex(Source.ExpirationIndex)
    {
    }

    void CommitPreparedState(FImpl& Prepared) noexcept
    {
        using std::swap;

        swap(NextEmissionId, Prepared.NextEmissionId);
        swap(NextSequence, Prepared.NextSequence);
        swap(InFlightEmissionId, Prepared.InFlightEmissionId);
        Records.swap(Prepared.Records);
        PriorityIndex.swap(Prepared.PriorityIndex);
        ExpirationIndex.swap(Prepared.ExpirationIndex);
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

    // El ID activo es sólo un localizador; cualquier valor no cero debe seguir
    // correspondiendo al record InFlight autoritativo.
    [[nodiscard]]
    bool HasInFlightEmission() const
    {
        if (InFlightEmissionId == 0)
        {
            return false;
        }

        const auto RecordIt = Records.find(InFlightEmissionId);

        if (
            RecordIt == Records.end()
            || RecordIt->second.State != EInternalEmissionState::InFlight
            || RecordIt->second.Envelope.EmissionId != InFlightEmissionId
        )
        {
            throw std::logic_error("Invalid in-flight emission state");
        }

        return true;
    }

    // El ID activo sólo es un localizador: antes de compararlo con la solicitud se
    // comprueba que continúe describiendo el único record InFlight autoritativo.
    [[nodiscard]]
    EInFlightTargetStatus ValidateInFlightTarget(
        FTSEmissionId RequestedEmissionId
    ) const
    {
        if (!HasInFlightEmission())
        {
            return EInFlightTargetStatus::NoInFlightEmission;
        }

        if (RequestedEmissionId != InFlightEmissionId)
        {
            return EInFlightTargetStatus::EmissionIdMismatch;
        }

        return EInFlightTargetStatus::Valid;
    }

    // El terminal se publica antes de retirar la fuente autoritativa. Si el vector
    // lanza, la emisión permanece íntegramente InFlight y puede reintentarse.
    void CompleteInFlight(
        ETSEmissionTerminalReason Reason,
        FTSEmissionLifecycleEvents& OutLifecycleEvents
    )
    {
        if (!HasInFlightEmission())
        {
            throw std::logic_error("Invalid in-flight emission state");
        }

        const auto RecordIt = Records.find(InFlightEmissionId);

        if (
            Reason != ETSEmissionTerminalReason::Confirmed
            && Reason != ETSEmissionTerminalReason::Cancelled
        )
        {
            throw std::logic_error("Invalid in-flight terminal reason");
        }

        FTSEmissionLifecycleEvent LifecycleEvent;
        LifecycleEvent.Envelope = RecordIt->second.Envelope;
        LifecycleEvent.Reason = Reason;

        OutLifecycleEvents.push_back(std::move(LifecycleEvent));

        // Eliminar el record libera el slot; los índices derivados conservan sólo
        // claves stale que sus validadores retirarán de forma lazy.
        Records.erase(RecordIt);
        InFlightEmissionId = 0;
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

    // Una única ruta sirve al Pump solicitado y al automático. El caller aporta la
    // instantánea temporal para impedir capturas divergentes dentro de una operación.
    [[nodiscard]]
    FTSPumpOutcome PumpAt(
        FTSEventQueueTimePoint Now,
        FTSEmissionLifecycleEvents& OutLifecycleEvents
    )
    {
        FTSPumpOutcome Outcome;
        ProcessDueExpirationsAt(Now, OutLifecycleEvents);

        if (HasInFlightEmission())
        {
            Outcome.Status = ETSPumpStatus::Busy;
            return Outcome;
        }

        DiscardStalePriorityEntries();

        if (PriorityIndex.empty())
        {
            Outcome.Status = ETSPumpStatus::QueueEmpty;
            return Outcome;
        }

        const FPriorityIndexEntry Entry = PriorityIndex.top();
        const auto RecordIt = Records.find(Entry.EmissionId);
        FInternalEmissionRecord& Record = RecordIt->second;

        Outcome.ReadyEmission = Record.Envelope;

        // El record permanece en Records y conserva el slot; el ID activo sólo impide
        // una segunda selección mientras su estado autoritativo sea InFlight.
        Record.State = EInternalEmissionState::InFlight;
        InFlightEmissionId = Record.Envelope.EmissionId;
        PriorityIndex.pop();

        // La clave temporal no se toca: el cambio de estado basta para volverla stale.
        Outcome.Status = ETSPumpStatus::EmissionReady;
        return Outcome;
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

TikStudioEventQueueSystem::FPreparedMutation::FPreparedMutation() noexcept =
    default;

TikStudioEventQueueSystem::FPreparedMutation::FPreparedMutation(
    std::unique_ptr<FImpl> InPreparedImpl
) noexcept
    : PreparedImpl(std::move(InPreparedImpl))
{
}

TikStudioEventQueueSystem::FPreparedMutation::~FPreparedMutation() = default;

TikStudioEventQueueSystem::FPreparedMutation::FPreparedMutation(
    FPreparedMutation&&
) noexcept = default;

TikStudioEventQueueSystem::FPreparedMutation&
TikStudioEventQueueSystem::FPreparedMutation::operator=(
    FPreparedMutation&&
) noexcept = default;

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

FTSUpdateFlowSettingsResult TikStudioEventQueueSystem::UpdateFlowSettings(
    ETSEventFlow Flow,
    const FTSFlowQueueSettings& NewSettings
)
{
    FTSUpdateFlowSettingsResult Result;
    Result.Flow = Flow;

    if (!IsValidFlow(Flow))
    {
        return Result;
    }

    if (NewSettings.TTL.count() < 0)
    {
        Result.Status = ETSUpdateFlowSettingsStatus::RejectedInvalidTTL;
        return Result;
    }

    switch (NewSettings.ExpirePolicy)
    {
    case ETSEventExpirePolicy::Discard:
    case ETSEventExpirePolicy::Consolidate:
        break;

    default:
        Result.Status =
            ETSUpdateFlowSettingsStatus::RejectedInvalidExpirePolicy;
        return Result;
    }

    // La validación concluye antes del único reemplazo. Records e índices no se
    // recorren: sus snapshots siguen gobernando todas las emisiones ya admitidas.
    Impl->Settings.Flows[ToIndex(Flow)] = NewSettings;
    Result.Status = ETSUpdateFlowSettingsStatus::Updated;
    return Result;
}

FTSEnqueueResult TikStudioEventQueueSystem::Enqueue(
    const FTSEnqueueRequest& Request
)
{
    return EnqueuePrepared(
        Request,
        [](const FTSEnqueueResult&) noexcept
        {
        }
    );
}

TikStudioEventQueueSystem::FPreparedEnqueue
TikStudioEventQueueSystem::PrepareEnqueue(
    const FTSEnqueueRequest& Request
)
{
    // Toda validación, captura temporal y asignación ocurre sobre un estado alterno;
    // ninguna autoridad cambia hasta que el callback externo acepte el resultado.
    FPreparedEnqueue Prepared;
    const FImpl::FAdmissionValidationResult Validation =
        Impl->ValidateEnqueueRequest(Request);

    switch (Validation.Status)
    {
    case FImpl::EAdmissionValidationStatus::InvalidFlow:
        Prepared.Result.Status = ETSEnqueueStatus::RejectedInvalidFlow;
        return Prepared;

    case FImpl::EAdmissionValidationStatus::Disabled:
        Prepared.Result.Status = ETSEnqueueStatus::RejectedDisabled;
        return Prepared;

    case FImpl::EAdmissionValidationStatus::InvalidTTL:
        Prepared.Result.Status = ETSEnqueueStatus::RejectedInvalidTTL;
        return Prepared;

    case FImpl::EAdmissionValidationStatus::Valid:
        break;
    }

    const FTSFlowQueueSettings FlowSettings = *Validation.FlowSettings;

    if (Impl->IsFlowAtCapacity(Request.Flow, FlowSettings.MaxSlots))
    {
        Prepared.Result.Status = ETSEnqueueStatus::RejectedAtCapacity;
        return Prepared;
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

    std::unique_ptr<FImpl> PreparedImpl = std::make_unique<FImpl>(
        *Impl,
        FImpl::FPreparedStateTag{}
    );

    FImpl::FEmissionIdentity Identity;

    if (!PreparedImpl->TryAllocateEmissionIdentity(Identity))
    {
        Prepared.Result.Status = ETSEnqueueStatus::RejectedIdentityExhausted;
        return Prepared;
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
    const auto [RecordIt, bInserted] = PreparedImpl->Records.emplace(
        Envelope.EmissionId,
        std::move(Record)
    );

    if (!bInserted)
    {
        throw std::logic_error("Duplicate emission identity");
    }

    // Los índices se construyen en el estado preparado; cualquier asignación fallida
    // descarta el plan completo sin tocar Records autoritativo.
    const FImpl::FInternalEmissionRecord& StoredRecord = RecordIt->second;
    const FTSEmissionEnvelope& StoredEnvelope = StoredRecord.Envelope;

    PreparedImpl->PriorityIndex.push(FImpl::FPriorityIndexEntry{
        StoredEnvelope.PriorityScore,
        StoredEnvelope.Sequence,
        StoredEnvelope.EmissionId,
        StoredRecord.Revision
    });

    if (StoredEnvelope.ExpiresAt != FTSEventQueueTimePoint::max())
    {
        PreparedImpl->ExpirationIndex.push(FImpl::FExpirationIndexEntry{
            StoredEnvelope.ExpiresAt,
            StoredEnvelope.Sequence,
            StoredEnvelope.EmissionId,
            StoredRecord.Revision
        });
    }

    Prepared.Result.Status = ETSEnqueueStatus::Accepted;
    Prepared.Result.AdmittedEmission = Envelope;

    // Auto Pump sólo se solicita después de una admisión completa y estando idle; usa
    // el mismo Now de Enqueue para ordenar expiraciones y selección coherentemente.
    if (
        PreparedImpl->Settings.Pump.bPumpAfterEnqueueWhenIdle
        && !PreparedImpl->HasInFlightEmission()
    )
    {
        Prepared.Result.AutoPumpOutcome = PreparedImpl->PumpAt(
            Now,
            Prepared.Result.LifecycleEvents
        );
    }

    Prepared.Mutation = FPreparedMutation(std::move(PreparedImpl));
    return Prepared;
}

FTSPumpResult TikStudioEventQueueSystem::Pump()
{
    return PumpPrepared(
        [](const FTSPumpResult&) noexcept
        {
        }
    );
}

TikStudioEventQueueSystem::FPreparedPump
TikStudioEventQueueSystem::PreparePump()
{
    FPreparedPump Prepared;
    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    std::unique_ptr<FImpl> PreparedImpl = std::make_unique<FImpl>(
        *Impl,
        FImpl::FPreparedStateTag{}
    );

    Prepared.Result.Outcome = PreparedImpl->PumpAt(
        Now,
        Prepared.Result.LifecycleEvents
    );
    Prepared.Mutation = FPreparedMutation(std::move(PreparedImpl));
    return Prepared;
}

FTSConfirmResult TikStudioEventQueueSystem::Confirm(
    FTSEmissionId EmissionId
)
{
    return ConfirmPrepared(
        EmissionId,
        [](const FTSConfirmResult&) noexcept
        {
        }
    );
}

TikStudioEventQueueSystem::FPreparedConfirm
TikStudioEventQueueSystem::PrepareConfirm(
    FTSEmissionId EmissionId
)
{
    FPreparedConfirm Prepared;

    switch (Impl->ValidateInFlightTarget(EmissionId))
    {
    case FImpl::EInFlightTargetStatus::NoInFlightEmission:
        return Prepared;

    case FImpl::EInFlightTargetStatus::EmissionIdMismatch:
        Prepared.Result.Status = ETSConfirmStatus::EmissionIdMismatch;
        return Prepared;

    case FImpl::EInFlightTargetStatus::Valid:
        break;
    }

    std::optional<FTSEventQueueTimePoint> Now;
    if (Impl->Settings.Pump.bPumpAfterConfirm)
    {
        // La captura pertenece a prepare: si falla, la emisión continúa InFlight.
        Now.emplace(Impl->CaptureNow());
    }

    std::unique_ptr<FImpl> PreparedImpl = std::make_unique<FImpl>(
        *Impl,
        FImpl::FPreparedStateTag{}
    );

    PreparedImpl->CompleteInFlight(
        ETSEmissionTerminalReason::Confirmed,
        Prepared.Result.LifecycleEvents
    );
    Prepared.Result.Status = ETSConfirmStatus::Confirmed;

    if (Now.has_value())
    {
        // Confirmed precede a cualquier terminal producido por el Auto Pump.
        Prepared.Result.AutoPumpOutcome = PreparedImpl->PumpAt(
            *Now,
            Prepared.Result.LifecycleEvents
        );
    }

    Prepared.Mutation = FPreparedMutation(std::move(PreparedImpl));
    return Prepared;
}

FTSCancelInFlightResult TikStudioEventQueueSystem::CancelInFlight(
    FTSEmissionId EmissionId
)
{
    return CancelInFlightPrepared(
        EmissionId,
        [](const FTSCancelInFlightResult&) noexcept
        {
        }
    );
}

TikStudioEventQueueSystem::FPreparedCancelInFlight
TikStudioEventQueueSystem::PrepareCancelInFlight(
    FTSEmissionId EmissionId
)
{
    FPreparedCancelInFlight Prepared;

    switch (Impl->ValidateInFlightTarget(EmissionId))
    {
    case FImpl::EInFlightTargetStatus::NoInFlightEmission:
        return Prepared;

    case FImpl::EInFlightTargetStatus::EmissionIdMismatch:
        Prepared.Result.Status =
            ETSCancelInFlightStatus::EmissionIdMismatch;
        return Prepared;

    case FImpl::EInFlightTargetStatus::Valid:
        break;
    }

    std::unique_ptr<FImpl> PreparedImpl = std::make_unique<FImpl>(
        *Impl,
        FImpl::FPreparedStateTag{}
    );
    PreparedImpl->CompleteInFlight(
        ETSEmissionTerminalReason::Cancelled,
        Prepared.Result.LifecycleEvents
    );
    Prepared.Result.Status = ETSCancelInFlightStatus::Cancelled;
    Prepared.Mutation = FPreparedMutation(std::move(PreparedImpl));
    return Prepared;
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
    return ProcessDueExpirationsPrepared(
        [](const FTSProcessDueExpirationsResult&) noexcept
        {
        }
    );
}

TikStudioEventQueueSystem::FPreparedProcessDueExpirations
TikStudioEventQueueSystem::PrepareProcessDueExpirations()
{
    FPreparedProcessDueExpirations Prepared;
    const FTSEventQueueTimePoint Now = Impl->CaptureNow();
    std::unique_ptr<FImpl> PreparedImpl = std::make_unique<FImpl>(
        *Impl,
        FImpl::FPreparedStateTag{}
    );
    PreparedImpl->ProcessDueExpirationsAt(
        Now,
        Prepared.Result.LifecycleEvents
    );
    Prepared.Mutation = FPreparedMutation(std::move(PreparedImpl));

    return Prepared;
}

void TikStudioEventQueueSystem::CommitPreparedMutation(
    FPreparedMutation& Mutation
) noexcept
{
    if (!Mutation.PreparedImpl)
    {
        return;
    }

    // El único commit intercambia estado ya construido; reset consume el token y
    // destruye la copia anterior sin dejar una segunda oportunidad de aplicación.
    Impl->CommitPreparedState(*Mutation.PreparedImpl);
    Mutation.PreparedImpl.reset();
}

TikStudioEventQueueSystem::~TikStudioEventQueueSystem() = default;

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    TikStudioEventQueueSystem&&
) noexcept = default;

TikStudioEventQueueSystem& TikStudioEventQueueSystem::operator=(
    TikStudioEventQueueSystem&&
) noexcept = default;
