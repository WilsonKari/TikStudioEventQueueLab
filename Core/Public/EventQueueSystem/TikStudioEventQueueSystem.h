#pragma once

#include "EventQueueSystem/TSEventQueueSystemOperations.h"
#include "EventQueueSystem/TSEventQueueSystemSettings.h"

#include <functional>
#include <memory>
#include <utility>

struct FTSEventQueueSystemTestAccess;

class TikStudioEventQueueSystem final
{
private:
    class FImpl;

    // La preparación posee un estado alternativo completo. Sólo el Core puede
    // intercambiarlo una vez con el estado autoritativo durante el commit.
    class FPreparedMutation final
    {
    public:
        FPreparedMutation() noexcept;
        ~FPreparedMutation();

        FPreparedMutation(const FPreparedMutation&) = delete;
        FPreparedMutation& operator=(const FPreparedMutation&) = delete;

        FPreparedMutation(FPreparedMutation&&) noexcept;
        FPreparedMutation& operator=(FPreparedMutation&&) noexcept;

    private:
        friend class TikStudioEventQueueSystem;

        explicit FPreparedMutation(std::unique_ptr<FImpl> PreparedImpl) noexcept;

        std::unique_ptr<FImpl> PreparedImpl;
    };

    struct FPreparedEnqueue
    {
        FTSEnqueueResult Result{};
        FPreparedMutation Mutation{};
    };

    struct FPreparedPump
    {
        FTSPumpResult Result{};
        FPreparedMutation Mutation{};
    };

    struct FPreparedUpdatePendingScheduling
    {
        FTSUpdatePendingSchedulingResult Result{};
        FPreparedMutation Mutation{};
    };

    struct FPreparedConfirm
    {
        FTSConfirmResult Result{};
        FPreparedMutation Mutation{};
    };

    struct FPreparedCancelInFlight
    {
        FTSCancelInFlightResult Result{};
        FPreparedMutation Mutation{};
    };

    struct FPreparedProcessDueExpirations
    {
        FTSProcessDueExpirationsResult Result{};
        FPreparedMutation Mutation{};
    };

public:
    // Un proveedor vacío usará FTSEventQueueClock::now(); uno válido producirá
    // el instante monotónico que se capturará una vez por operación pública.
    explicit TikStudioEventQueueSystem(
        FTSEventQueueSettings Settings = FTSEventQueueSettings{},
        FTSNowProvider NowProvider = {}
    );

    ~TikStudioEventQueueSystem();

    TikStudioEventQueueSystem(
        const TikStudioEventQueueSystem&
    ) = delete;

    TikStudioEventQueueSystem& operator=(
        const TikStudioEventQueueSystem&
    ) = delete;

    TikStudioEventQueueSystem(
        TikStudioEventQueueSystem&&
    ) noexcept;

    TikStudioEventQueueSystem& operator=(
        TikStudioEventQueueSystem&&
    ) noexcept;

    [[nodiscard]]
    FTSEnqueueResult Enqueue(const FTSEnqueueRequest& Request);

    // El callback puede validar y reservar autoridades externas. Si lanza, el Core
    // permanece intacto; al retornar, el commit interno es noexcept y de un solo uso.
    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSEnqueueResult EnqueuePrepared(
        const FTSEnqueueRequest& Request,
        TPrepareCallback&& PrepareCallback
    )
    {
        FPreparedEnqueue Prepared = PrepareEnqueue(Request);
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSEnqueueResult&>(Prepared.Result)
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

    // Reemplaza los settings usados por admisiones futuras; las emisiones vivas
    // conservan los snapshots efectivos capturados al admitirse.
    [[nodiscard]]
    FTSUpdateFlowSettingsResult UpdateFlowSettings(
        ETSEventFlow Flow,
        const FTSFlowQueueSettings& NewSettings
    );

    [[nodiscard]]
    FTSUpdatePendingSchedulingResult UpdatePendingScheduling(
        const FTSUpdatePendingSchedulingRequest& Request
    );

    // ObservedAt permite que el caller prepare autoridades externas usando la
    // misma frontera temporal. Una excepción del callback preserva el Core.
    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSUpdatePendingSchedulingResult UpdatePendingSchedulingPrepared(
        const FTSUpdatePendingSchedulingRequest& Request,
        FTSEventQueueTimePoint ObservedAt,
        TPrepareCallback&& PrepareCallback
    )
    {
        FPreparedUpdatePendingScheduling Prepared =
            PrepareUpdatePendingScheduling(Request, ObservedAt);
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSUpdatePendingSchedulingResult&>(
                Prepared.Result
            )
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

    // La referencia permanece válida únicamente durante el callback, no debe
    // conservarse ni usarse para mutar reentrantemente esta instancia. Sólo expone
    // el snapshot público, nunca el record interno ni sus índices derivados.
    template <typename TCallback>
    [[nodiscard]]
    bool VisitLiveEmission(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
        const FTSEmissionEnvelope* Emission = FindLiveEmission(EmissionId);
        if (Emission == nullptr)
        {
            return false;
        }

        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const FTSEmissionEnvelope&>(*Emission)
        );
        return true;
    }

    [[nodiscard]]
    bool IsPendingEmission(FTSEmissionId EmissionId) const noexcept;

    [[nodiscard]]
    FTSPumpResult Pump();

    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSPumpResult PumpPrepared(TPrepareCallback&& PrepareCallback)
    {
        FPreparedPump Prepared = PreparePump();
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSPumpResult&>(Prepared.Result)
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

    [[nodiscard]]
    FTSConfirmResult Confirm(FTSEmissionId EmissionId);

    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSConfirmResult ConfirmPrepared(
        FTSEmissionId EmissionId,
        TPrepareCallback&& PrepareCallback
    )
    {
        FPreparedConfirm Prepared = PrepareConfirm(EmissionId);
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSConfirmResult&>(Prepared.Result)
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

    [[nodiscard]]
    FTSProcessDueExpirationsResult ProcessDueExpirations();

    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSProcessDueExpirationsResult ProcessDueExpirationsPrepared(
        TPrepareCallback&& PrepareCallback
    )
    {
        FPreparedProcessDueExpirations Prepared =
            PrepareProcessDueExpirations();
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSProcessDueExpirationsResult&>(Prepared.Result)
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

    [[nodiscard]]
    FTSNextWakeTimeResult GetNextWakeTime();

    [[nodiscard]]
    FTSCancelInFlightResult CancelInFlight(FTSEmissionId EmissionId);

    template <typename TPrepareCallback>
    [[nodiscard]]
    FTSCancelInFlightResult CancelInFlightPrepared(
        FTSEmissionId EmissionId,
        TPrepareCallback&& PrepareCallback
    )
    {
        FPreparedCancelInFlight Prepared =
            PrepareCancelInFlight(EmissionId);
        std::invoke(
            std::forward<TPrepareCallback>(PrepareCallback),
            static_cast<const FTSCancelInFlightResult&>(Prepared.Result)
        );
        CommitPreparedMutation(Prepared.Mutation);
        return std::move(Prepared.Result);
    }

private:
    [[nodiscard]]
    FPreparedEnqueue PrepareEnqueue(const FTSEnqueueRequest& Request);

    [[nodiscard]]
    FPreparedUpdatePendingScheduling PrepareUpdatePendingScheduling(
        const FTSUpdatePendingSchedulingRequest& Request,
        FTSEventQueueTimePoint ObservedAt
    );

    [[nodiscard]]
    FPreparedPump PreparePump();

    [[nodiscard]]
    FPreparedConfirm PrepareConfirm(FTSEmissionId EmissionId);

    [[nodiscard]]
    FPreparedCancelInFlight PrepareCancelInFlight(
        FTSEmissionId EmissionId
    );

    [[nodiscard]]
    FPreparedProcessDueExpirations PrepareProcessDueExpirations();

    // Un token vacío representa una operación sin mutación; uno válido se consume
    // mediante un único intercambio noexcept del estado preparado.
    void CommitPreparedMutation(FPreparedMutation& Mutation) noexcept;

    [[nodiscard]]
    const FTSEmissionEnvelope* FindLiveEmission(
        FTSEmissionId EmissionId
    ) const noexcept;

    [[nodiscard]]
    bool SetPendingRevisionForTesting(
        FTSEmissionId EmissionId,
        std::uint64_t Revision
    );

    friend struct FTSEventQueueSystemTestAccess;

    std::unique_ptr<FImpl> Impl;
};
