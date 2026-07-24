#pragma once

#include "EventPipeline/Coordinator/TSEventPipelineCoordinator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

enum class ETSEventHostCommandKind : std::uint8_t
{
    None,
    ChatInput,
    FollowInput,
    ChatCompletion,
    FollowCompletion,
    ShareInput,
    ShareCompletion,
    LikeInput,
    LikeCompletion,
    RoomUserInput,
    RoomUserCompletion,
    GiftInput,
    GiftCompletion,
    MemberInput,
    MemberCompletion,
    FlowSettingsUpdate,
    GiftComboInput,
    GiftComboCompletion,
    ShareMilestoneInput,
    ShareMilestoneCompletion
};

// El variant sólo transporta el resultado propietario fuera del Host; no forma parte
// de los contratos del Pipeline ni del Core.
using FTSEventProcessingDispatch = std::variant<
    FTSChatProcessingDispatch,
    FTSFollowProcessingDispatch,
    FTSShareProcessingDispatch,
    FTSLikeProcessingDispatch,
    FTSRoomUserProcessingDispatch,
    FTSGiftProcessingDispatch,
    FTSMemberProcessingDispatch,
    FTSGiftComboProcessingDispatch,
    FTSShareMilestoneProcessingDispatch
>;

struct FTSEventHostCycleResult
{
    ETSEventHostCommandKind ProcessedCommand =
        ETSEventHostCommandKind::None;

    FTSProcessDueExpirationsResult DueExpirations{};

    // El tipo de comando determina cuál de estos resultados puede existir.
    std::optional<FTSPipelineAdmissionResult> AdmissionResult;
    std::optional<FTSProcessingCompletionResult> CompletionResult;
    std::optional<FTSUpdateFlowSettingsResult> FlowSettingsUpdateResult;

    std::optional<FTSPumpResult> PumpResult;
    std::optional<FTSEventProcessingDispatch> Dispatch;
    FTSNextWakeTimeResult NextWakeTime{};
    bool bMoreCommandsPending = false;
};

// Todas las rutas operativas comparten este owner thread, Coordinator y Core para
// preservar un único InFlight y un orden operativo global.
class FTSEventExecutionHost final
{
public:
    explicit FTSEventExecutionHost(
        FTSEventQueueSettings CoreSettings = {},
        FTSNowProvider NowProvider = {},
        FTSEventPipelineSettings PipelineSettings = {}
    );

    // Los productores deben detenerse y finalizar antes de destruir el Host; el
    // destructor no coordina publicaciones concurrentes ni implementa shutdown.
    ~FTSEventExecutionHost();

    FTSEventExecutionHost(const FTSEventExecutionHost&) = delete;
    FTSEventExecutionHost& operator=(const FTSEventExecutionHost&) = delete;

    FTSEventExecutionHost(FTSEventExecutionHost&&) = delete;
    FTSEventExecutionHost& operator=(FTSEventExecutionHost&&) = delete;

    [[nodiscard]]
    bool PostChat(FTSChatInput Input);

    [[nodiscard]]
    bool PostFollow(FTSFollowInput Input);

    [[nodiscard]]
    bool PostShare(FTSShareInput Input);

    [[nodiscard]]
    bool PostShareMilestone(FTSShareInput Input);

    [[nodiscard]]
    bool PostLike(FTSLikeInput Input);

    [[nodiscard]]
    bool PostRoomUser(FTSRoomUserInput Input);

    [[nodiscard]]
    bool PostGift(FTSGiftInput Input);

    [[nodiscard]]
    bool PostGiftCombo(FTSGiftInput Input);

    [[nodiscard]]
    bool PostMember(FTSMemberInput Input);

    [[nodiscard]]
    bool PostChatCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostFollowCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostShareCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostShareMilestoneCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostLikeCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostRoomUserCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostGiftCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostGiftComboCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostMemberCompletion(
        FTSEmissionId EmissionId,
        ETSProcessingResult ProcessingResult
    );

    [[nodiscard]]
    bool PostFlowSettingsUpdate(
        ETSEventFlow Flow,
        FTSFlowQueueSettings NewSettings
    );

    // Sólo el hilo que construyó el Host puede ejecutar el Coordinator.
    [[nodiscard]]
    FTSEventHostCycleResult RunOneCycle();

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
