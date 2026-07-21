#pragma once

#include "EventQueueSystem/TSEventQueueSystemOperations.h"

#include <cstdint>
#include <optional>

enum class ETSEventFamilyKind : std::uint8_t
{
    Chat,
    Gift,
    Like,
    Member,
    RoomUser,
    Share,
    Follow
};

// Única autoridad portable para las rutas operativas. Los flujos derivados siguen
// reservados aunque sean valores válidos del enum del Core.
[[nodiscard]]
constexpr bool IsSupportedFamilyFlowPair(
    ETSEventFamilyKind FamilyKind,
    ETSEventFlow Flow
) noexcept
{
    return
        (FamilyKind == ETSEventFamilyKind::Chat &&
            Flow == ETSEventFlow::Chat) ||
        (FamilyKind == ETSEventFamilyKind::Gift &&
            Flow == ETSEventFlow::Gift) ||
        (FamilyKind == ETSEventFamilyKind::Follow &&
            Flow == ETSEventFlow::Follow) ||
        (FamilyKind == ETSEventFamilyKind::Like &&
            Flow == ETSEventFlow::Like) ||
        (FamilyKind == ETSEventFamilyKind::Member &&
            Flow == ETSEventFlow::Member) ||
        (FamilyKind == ETSEventFamilyKind::RoomUser &&
            Flow == ETSEventFlow::RoomUser) ||
        (FamilyKind == ETSEventFamilyKind::Share &&
            Flow == ETSEventFlow::Share);
}

// Identidad opaca dentro del repositorio tipado de una familia. Cero es inválido y
// este valor nunca sustituye la identidad global asignada por el core.
struct FTSPayloadHandle
{
    std::uint64_t Value = 0;
};

enum class ETSExternalEmissionState : std::uint8_t
{
    Bound,
    Processing,
    TerminalPendingHandling
};

enum class ETSProcessingResult : std::uint8_t
{
    Succeeded,
    Cancelled,
    Failed
};

template <typename TPayload>
struct TTSAdmissionCandidate
{
    ETSEventFamilyKind FamilyKind = ETSEventFamilyKind::Chat;
    FTSEnqueueRequest EnqueueRequest{};
    TPayload Payload{};
};

// Una decisión vacía significa NoEmission; cuando existe, el payload conserva su
// tipo concreto hasta que la futura familia lo admita en su repositorio.
template <typename TPayload>
using TTSFamilyDecision = std::optional<TTSAdmissionCandidate<TPayload>>;

struct FTSEmissionBinding
{
    // EmissionId es la única clave global. Familia y flujo sólo verifican y enrutan.
    FTSEmissionId EmissionId = 0;
    ETSEventFamilyKind FamilyKind = ETSEventFamilyKind::Chat;
    ETSEventFlow ExpectedFlow = ETSEventFlow::Chat;
    FTSPayloadHandle PayloadHandle{};
    ETSExternalEmissionState ExternalState =
        ETSExternalEmissionState::Bound;
};
