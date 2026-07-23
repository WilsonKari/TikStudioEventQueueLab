#include "Flows/Chat/TSChatScenarioProvider.h"

#include "EventPipeline/Priority/TSCommonUserPriorityPolicy.h"
#include "EventPipeline/Settings/TSChatSemanticSettings.h"
#include "Shared/TSScenarioConsole.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace
{
    [[nodiscard]]
    std::string PromptWithDefault(
        std::string_view Label,
        std::int64_t DefaultValue,
        std::string_view Suffix = {}
    )
    {
        return std::string(Label) + " [" + std::to_string(DefaultValue) +
            std::string(Suffix) + "]: ";
    }

    [[nodiscard]]
    std::string PromptWithDefault(
        std::string_view Label,
        std::string_view DefaultValue
    )
    {
        return std::string(Label) + " [" + std::string(DefaultValue) + "]: ";
    }

    [[nodiscard]]
    std::string YesNoPrompt(
        std::string_view Label,
        bool DefaultValue
    )
    {
        return std::string(Label) +
            (DefaultValue ? " [S/n]: " : " [s/N]: ");
    }

    [[nodiscard]]
    std::int64_t CalculateEstimatedPriorityScore(
        std::int32_t BaseWeight,
        std::int64_t Adjustment
    ) noexcept
    {
        const std::int64_t Base = static_cast<std::int64_t>(BaseWeight);
        constexpr std::int64_t Minimum =
            std::numeric_limits<std::int64_t>::min();
        constexpr std::int64_t Maximum =
            std::numeric_limits<std::int64_t>::max();

        if (Base > 0 && Adjustment > Maximum - Base)
        {
            return Maximum;
        }
        if (Base < 0 && Adjustment < Minimum - Base)
        {
            return Minimum;
        }
        return Base + Adjustment;
    }

    [[nodiscard]]
    FTSCommonUserPriorityBreakdown EvaluateUser(
        const FTSUserSnapshot& User,
        const FTSEventPipelineSettings& Settings
    )
    {
        return FTSCommonUserPriorityPolicy::Evaluate(
            User,
            Settings.CommonUserPriority
        );
    }

    void ShowUserPriorityPreview(
        FTSScenarioConsole& Console,
        std::string_view Label,
        const FTSUserSnapshot& User,
        std::int32_t BaseWeight,
        const FTSEventPipelineSettings& Settings
    )
    {
        const FTSCommonUserPriorityBreakdown Breakdown =
            EvaluateUser(User, Settings);
        Console.WriteLine(
            std::string(Label) + ": CommonPriorityAdjustment=" +
            std::to_string(Breakdown.TotalAdjustment) +
            ", PriorityScore inicial estimado=" +
            std::to_string(CalculateEstimatedPriorityScore(
                BaseWeight,
                Breakdown.TotalAdjustment
            ))
        );
    }

    void ConfigureUser(
        FTSScenarioConsole& Console,
        std::string_view Label,
        FTSUserSnapshot& User,
        std::string_view OtherUniqueId,
        std::int32_t BaseWeight,
        const FTSEventPipelineSettings& Settings
    )
    {
        Console.WriteLine();
        Console.WriteLine(std::string("--- ") + std::string(Label) + " ---");
        while (true)
        {
            const std::string UniqueId = Console.ReadNonEmptyString(
                PromptWithDefault("UniqueId", User.UniqueId),
                User.UniqueId
            );
            if (UniqueId != OtherUniqueId)
            {
                User.UniqueId = UniqueId;
                break;
            }
            Console.WriteLine(
                "Los usuarios A y B deben tener UniqueId diferentes."
            );
        }

        User.Nickname = Console.ReadString(
            PromptWithDefault("Nickname", User.Nickname),
            User.Nickname
        );
        User.FollowRole = static_cast<std::int32_t>(Console.ReadInteger(
            PromptWithDefault("FollowRole", User.FollowRole),
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max(),
            User.FollowRole
        ));
        User.bIsModerator = Console.ReadYesNo(
            YesNoPrompt("¿Es moderador?", User.bIsModerator),
            User.bIsModerator
        );
        User.bIsSubscriber = Console.ReadYesNo(
            YesNoPrompt("¿Es suscriptor?", User.bIsSubscriber),
            User.bIsSubscriber
        );
        User.bIsNewGifter = Console.ReadYesNo(
            YesNoPrompt("¿Es nuevo gifter?", User.bIsNewGifter),
            User.bIsNewGifter
        );
        User.TopGifterRank = static_cast<std::int32_t>(Console.ReadInteger(
            PromptWithDefault("TopGifterRank", User.TopGifterRank),
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max(),
            User.TopGifterRank
        ));
        User.GifterLevel = static_cast<std::int32_t>(Console.ReadInteger(
            PromptWithDefault("GifterLevel", User.GifterLevel),
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max(),
            User.GifterLevel
        ));
        User.TeamMemberLevel =
            static_cast<std::int32_t>(Console.ReadInteger(
                PromptWithDefault(
                    "TeamMemberLevel",
                    User.TeamMemberLevel
                ),
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max(),
                User.TeamMemberLevel
            ));

        ShowUserPriorityPreview(
            Console,
            Label,
            User,
            BaseWeight,
            Settings
        );
    }

    [[nodiscard]]
    std::string DescribeUser(
        std::string_view Label,
        const FTSUserSnapshot& User,
        std::int32_t BaseWeight,
        const FTSEventPipelineSettings& Settings
    )
    {
        const FTSCommonUserPriorityBreakdown Breakdown =
            EvaluateUser(User, Settings);
        std::ostringstream Stream;
        Stream << Label << ": UniqueId=" << User.UniqueId
            << ", Nickname=" << User.Nickname
            << ", FollowRole=" << User.FollowRole
            << ", Moderator=" << (User.bIsModerator ? "sí" : "no")
            << ", Subscriber=" << (User.bIsSubscriber ? "sí" : "no")
            << ", NewGifter=" << (User.bIsNewGifter ? "sí" : "no")
            << ", TopGifterRank=" << User.TopGifterRank
            << ", GifterLevel=" << User.GifterLevel
            << ", TeamMemberLevel=" << User.TeamMemberLevel
            << ", CommonPriorityAdjustment=" << Breakdown.TotalAdjustment
            << ", PriorityScore estimado="
            << CalculateEstimatedPriorityScore(
                BaseWeight,
                Breakdown.TotalAdjustment
            );
        return Stream.str();
    }

    [[nodiscard]]
    std::chrono::milliseconds AddArrivalInterval(
        std::chrono::milliseconds Current,
        std::chrono::milliseconds Interval
    )
    {
        if (Current.count() < 0 || Interval.count() < 0)
        {
            throw std::invalid_argument(
                "Chat arrival intervals cannot be negative"
            );
        }

        constexpr std::int64_t Maximum =
            std::numeric_limits<std::int64_t>::max();
        if (Current.count() > Maximum - Interval.count())
        {
            throw std::overflow_error("Chat arrival offset overflow");
        }

        return std::chrono::milliseconds{
            Current.count() + Interval.count()
        };
    }
}

ETSEventFlow FTSChatScenarioProvider::GetObservedFlow() const noexcept
{
    return ETSEventFlow::Chat;
}

std::string_view FTSChatScenarioProvider::GetDisplayName() const noexcept
{
    return "Chat";
}

FTSScenarioDefinition FTSChatScenarioProvider::Configure(
    FTSScenarioConsole& Console
) const
{
    FTSScenarioDefinition Scenario;
    Console.WriteLine("\nConfigura el runtime simulado (no son settings de Core).");
    Scenario.RuntimeSettings.ProcessingDuration = Console.ReadMilliseconds(
        PromptWithDefault(
            "Duración de procesamiento",
            Scenario.RuntimeSettings.ProcessingDuration.count(),
            " ms"
        ),
        std::chrono::milliseconds{1},
        Scenario.RuntimeSettings.ProcessingDuration
    );
    Scenario.RuntimeSettings.DefaultArrivalInterval =
        Console.ReadMilliseconds(
            PromptWithDefault(
                "Intervalo predeterminado entre mensajes",
                Scenario.RuntimeSettings.DefaultArrivalInterval.count(),
                " ms"
            ),
            std::chrono::milliseconds{0},
            Scenario.RuntimeSettings.DefaultArrivalInterval
        );

    FTSFlowQueueSettings& ChatFlow =
        Scenario.CoreSettings.Flows[ToIndex(ETSEventFlow::Chat)];
    Console.WriteLine("\nConfigura el flow Chat en Core.");
    ChatFlow.BaseWeight = static_cast<std::int32_t>(Console.ReadInteger(
        PromptWithDefault("BaseWeight", ChatFlow.BaseWeight),
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        ChatFlow.BaseWeight
    ));
    ChatFlow.TTL = Console.ReadMilliseconds(
        PromptWithDefault("TTL", ChatFlow.TTL.count(), " ms"),
        std::chrono::milliseconds{0},
        ChatFlow.TTL
    );
    ChatFlow.MaxSlots = static_cast<std::uint32_t>(Console.ReadInteger(
        PromptWithDefault("MaxSlots (0 significa sin capacidad)", ChatFlow.MaxSlots),
        0,
        std::numeric_limits<std::uint32_t>::max(),
        ChatFlow.MaxSlots
    ));

    Console.WriteLine("\nConfigura la semántica acumulativa de Chat.");
    Scenario.PipelineSettings.Chat.MaxMessagesPerBatch =
        static_cast<std::uint32_t>(Console.ReadInteger(
            PromptWithDefault(
                "MaxMessagesPerBatch",
                Scenario.PipelineSettings.Chat.MaxMessagesPerBatch
            ),
            1,
            std::numeric_limits<std::uint32_t>::max(),
            Scenario.PipelineSettings.Chat.MaxMessagesPerBatch
        ));
    Scenario.PipelineSettings.Chat.bRefreshTtlOnAppend =
        Console.ReadYesNo(
            YesNoPrompt(
                "¿Renovar TTL al acumular?",
                Scenario.PipelineSettings.Chat.bRefreshTtlOnAppend
            ),
            Scenario.PipelineSettings.Chat.bRefreshTtlOnAppend
        );

    if (!AreChatSemanticSettingsValid(Scenario.PipelineSettings.Chat))
    {
        throw std::invalid_argument(
            "La configuración semántica Chat resultante no es válida"
        );
    }

    if (!FTSCommonUserPriorityPolicy::AreSettingsValid(
            Scenario.PipelineSettings.CommonUserPriority
        ))
    {
        throw std::logic_error(
            "Los defaults de prioridad común no son válidos"
        );
    }

    FTSUserSnapshot UserA;
    UserA.UniqueId = "user-a";
    UserA.Nickname = "Usuario A";
    FTSUserSnapshot UserB;
    UserB.UniqueId = "user-b";
    UserB.Nickname = "Usuario B";

    ConfigureUser(
        Console,
        "Usuario A",
        UserA,
        UserB.UniqueId,
        ChatFlow.BaseWeight,
        Scenario.PipelineSettings
    );
    ConfigureUser(
        Console,
        "Usuario B",
        UserB,
        UserA.UniqueId,
        ChatFlow.BaseWeight,
        Scenario.PipelineSettings
    );

    std::chrono::milliseconds ArrivalOffset{0};
    std::uint64_t Sequence = 1;
    bool bAddAnother = false;
    do
    {
        Console.WriteLine("\nNuevo mensaje:");
        Console.WriteLine("1. Usuario A");
        Console.WriteLine("2. Usuario B");
        const std::size_t UserSelection = static_cast<std::size_t>(
            Console.ReadInteger("Usuario: ", 1, 2)
        );
        FTSUserSnapshot& SelectedUser =
            UserSelection == 1 ? UserA : UserB;
        const FTSUserSnapshot& OtherUser =
            UserSelection == 1 ? UserB : UserA;
        const std::string_view UserLabel =
            UserSelection == 1 ? "Usuario A" : "Usuario B";

        if (Console.ReadYesNo(
                "¿Editar el perfil guardado antes del mensaje? [s/N]: ",
                false
            ))
        {
            ConfigureUser(
                Console,
                UserLabel,
                SelectedUser,
                OtherUser.UniqueId,
                ChatFlow.BaseWeight,
                Scenario.PipelineSettings
            );
        }

        FTSChatInput Input;
        Input.User = SelectedUser;
        Input.Comment = Console.ReadString(
            "Comentario (puede quedar vacío): "
        );

        const std::chrono::milliseconds DefaultInterval =
            Scenario.Inputs.empty()
                ? std::chrono::milliseconds{0}
                : Scenario.RuntimeSettings.DefaultArrivalInterval;
        const std::chrono::milliseconds Interval =
            Console.ReadMilliseconds(
                PromptWithDefault(
                    "Intervalo desde el mensaje anterior",
                    DefaultInterval.count(),
                    " ms"
                ),
                std::chrono::milliseconds{0},
                DefaultInterval
            );
        ArrivalOffset = AddArrivalInterval(ArrivalOffset, Interval);
        Console.WriteLine(
            "ArrivalOffset resultante: " +
            std::to_string(ArrivalOffset.count()) + " ms"
        );

        std::ostringstream Description;
        Description << UserLabel << " (" << Input.User.UniqueId
            << "): \"" << Input.Comment << '"';

        Scenario.Inputs.push_back(FTSScheduledScenarioInput{
            Sequence,
            ArrivalOffset,
            Description.str(),
            [Input](FTSEventExecutionHost& Host)
            {
                (void)Host.PostChat(Input);
            }
        });

        if (Sequence == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Chat input sequence exhausted");
        }
        ++Sequence;
        bAddAnother = Console.ReadYesNo(
            "¿Agregar otro mensaje? [s/N]: ",
            false
        );
    }
    while (bAddAnother);

    Scenario.ConfigurationDetailLines.push_back(
        "MaxMessagesPerBatch: " +
        std::to_string(
            Scenario.PipelineSettings.Chat.MaxMessagesPerBatch
        )
    );
    Scenario.ConfigurationDetailLines.push_back(
        std::string("Refresh TTL al acumular: ") +
        (Scenario.PipelineSettings.Chat.bRefreshTtlOnAppend ? "sí" : "no")
    );
    Scenario.ConfigurationDetailLines.push_back(
        DescribeUser(
            "Usuario A",
            UserA,
            ChatFlow.BaseWeight,
            Scenario.PipelineSettings
        )
    );
    Scenario.ConfigurationDetailLines.push_back(
        DescribeUser(
            "Usuario B",
            UserB,
            ChatFlow.BaseWeight,
            Scenario.PipelineSettings
        )
    );
    return Scenario;
}

FTSObservedScenarioDispatch FTSChatScenarioProvider::ObserveDispatch(
    const FTSEventProcessingDispatch& Dispatch
) const
{
    const FTSChatProcessingDispatch* const ChatDispatch =
        std::get_if<FTSChatProcessingDispatch>(&Dispatch);
    if (ChatDispatch == nullptr)
    {
        throw std::logic_error(
            "Chat scenario provider received a non-Chat dispatch"
        );
    }

    FTSObservedScenarioDispatch Observed;
    Observed.Emission = ChatDispatch->Emission;
    Observed.DetailLines.push_back(
        "Usuario final: UniqueId=" + ChatDispatch->Payload.User.UniqueId +
        ", Nickname=" + ChatDispatch->Payload.User.Nickname
    );
    Observed.DetailLines.push_back(
        "CommonPriorityAdjustment=" +
        std::to_string(ChatDispatch->Payload.CommonPriorityAdjustment)
    );
    Observed.DetailLines.push_back(
        "Mensajes=" +
        std::to_string(ChatDispatch->Payload.Messages.size())
    );

    std::size_t MessageIndex = 0;
    for (const FTSChatMessageEntry& Message :
        ChatDispatch->Payload.Messages)
    {
        const std::int64_t ReceivedAtMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Message.ReceivedAt.time_since_epoch()
            ).count();
        std::ostringstream Detail;
        Detail << "Mensaje " << ++MessageIndex
            << ": texto=\"" << Message.Comment << "\""
            << ", ReceivedAt=" << ReceivedAtMilliseconds << " ms"
            << ", Command=" << (Message.bIsCommand ? "sí" : "no")
            << ", Emotes=" << Message.Emotes.size();
        Observed.DetailLines.push_back(Detail.str());
    }

    const FTSEmissionId EmissionId = ChatDispatch->Emission.EmissionId;
    Observed.PostSucceededCompletion =
        [EmissionId](FTSEventExecutionHost& Host)
        {
            (void)Host.PostChatCompletion(
                EmissionId,
                ETSProcessingResult::Succeeded
            );
        };
    return Observed;
}
