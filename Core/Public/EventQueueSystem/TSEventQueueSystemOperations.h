#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <chrono>
#include <cstdint>
#include <vector>

struct FTSEnqueueRequest
{
    ETSEventFlow Flow = ETSEventFlow::Chat;
    std::int64_t PriorityAdjustment = 0;
    // TTLOverride se usa sólo cuando bOverrideTTL es true. Si es false, se usa
    // el TTL del flujo. Un valor positivo expira mientras permanezca Pending,
    // cero significa sin expiración y un valor negativo es inválido. Sin
    // expiración se representará con FTSEventQueueTimePoint::max() y no se
    // añadirá al futuro índice de expiración.
    bool bOverrideTTL = false;
    std::chrono::milliseconds TTLOverride{0};
    bool bProtectedFromEviction = false;
};

enum class ETSEmissionTerminalReason : std::uint8_t
{
    // La emisión InFlight terminó correctamente mediante Confirm.
    Confirmed,
    // La emisión Pending venció con política Discard.
    ExpiredDiscard,
    // La emisión Pending venció con política Consolidate. El core sólo la
    // elimina y reporta; la familia decide cómo consolidar su payload.
    ExpiredConsolidate,
    // La futura política de evicción eliminó una emisión Pending.
    Evicted,
    // Una emisión InFlight fue cancelada explícitamente.
    Cancelled
};

// Comunica una transición terminal ya ocurrida. El envelope es un snapshot
// público y no constituye estado autoritativo del core.
struct FTSEmissionLifecycleEvent
{
    FTSEmissionEnvelope Envelope{};
    ETSEmissionTerminalReason Reason = ETSEmissionTerminalReason::Confirmed;
};

// Conserva el orden en que las transiciones terminales ocurrieron dentro de
// una operación; no se reordena por ID, flujo ni prioridad.
using FTSEmissionLifecycleEvents = std::vector<FTSEmissionLifecycleEvent>;

enum class ETSPumpStatus : std::uint8_t
{
    NotRequested,
    EmissionReady,
    QueueEmpty,
    Busy
};

// Resultado exclusivo de la selección. NotRequested sólo representa un Auto
// Pump no ejecutado; un Pump público directo no deberá producir ese estado.
struct FTSPumpOutcome
{
    ETSPumpStatus Status = ETSPumpStatus::NotRequested;
    // Sólo es significativo cuando Status == ETSPumpStatus::EmissionReady.
    FTSEmissionEnvelope ReadyEmission{};
};

// Resultado completo de una llamada pública a Pump. Los terminales pueden
// incluir expiraciones procesadas antes de seleccionar una emisión.
struct FTSPumpResult
{
    FTSPumpOutcome Outcome{};
    FTSEmissionLifecycleEvents LifecycleEvents;
};

enum class ETSEnqueueStatus : std::uint8_t
{
    Accepted,
    AcceptedWithEviction,
    RejectedInvalidFlow,
    RejectedDisabled,
    // El TTL efectivo, proveniente del override o de los settings, es negativo.
    RejectedInvalidTTL,
    // No puede producirse un nuevo par válido de EmissionId y Sequence.
    RejectedIdentityExhausted,
    RejectedAtCapacity
};

struct FTSEnqueueResult
{
    ETSEnqueueStatus Status = ETSEnqueueStatus::RejectedInvalidFlow;
    // Sólo es significativo para Accepted o AcceptedWithEviction.
    FTSEmissionEnvelope AdmittedEmission{};
    // Representa únicamente la selección automática, sin terminales.
    FTSPumpOutcome AutoPumpOutcome{};
    // Contiene expiraciones o evicciones producidas durante Enqueue.
    FTSEmissionLifecycleEvents LifecycleEvents;
};

enum class ETSConfirmStatus : std::uint8_t
{
    Confirmed,
    NoInFlightEmission,
    EmissionIdMismatch
};

struct FTSConfirmResult
{
    ETSConfirmStatus Status = ETSConfirmStatus::NoInFlightEmission;
    FTSPumpOutcome AutoPumpOutcome{};
    // El éxito produce exactamente un Confirmed para la emisión InFlight y
    // puede incluir otros terminales; un fallo no produce Confirmed.
    FTSEmissionLifecycleEvents LifecycleEvents;
};

// Terminales generados al expirar emisiones Pending vencidas. La futura
// operación capturará Now una vez y eliminará cada emisión exactamente una vez.
struct FTSProcessDueExpirationsResult
{
    FTSEmissionLifecycleEvents LifecycleEvents;
};

enum class ETSNextWakeStatus : std::uint8_t
{
    NoWakeScheduled,
    WakeScheduled
};

struct FTSNextWakeTimeResult
{
    // NoWakeScheduled significa que no hay Pending con expiración finita.
    ETSNextWakeStatus Status = ETSNextWakeStatus::NoWakeScheduled;
    // Sólo es significativo cuando Status == ETSNextWakeStatus::WakeScheduled.
    FTSEventQueueTimePoint WakeTime{};
};

enum class ETSCancelInFlightStatus : std::uint8_t
{
    Cancelled,
    NoInFlightEmission,
    EmissionIdMismatch
};

struct FTSCancelInFlightResult
{
    ETSCancelInFlightStatus Status =
        ETSCancelInFlightStatus::NoInFlightEmission;
    // Cancelled produce un único terminal Cancelled. Esta operación no hace
    // Auto Pump; el host puede llamar Pump explícitamente.
    FTSEmissionLifecycleEvents LifecycleEvents;
};
