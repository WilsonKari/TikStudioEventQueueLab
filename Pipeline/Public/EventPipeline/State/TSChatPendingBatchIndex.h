#pragma once

#include "EventQueueSystem/TSEventQueueSystemTypes.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

struct FTSChatPendingBatchEntry
{
    FTSEmissionId EmissionId = 0;
    FTSEventQueueTimePoint ExpiresAt{};
};

// Autoridad tipada para el único lote Chat todavía mutable de cada usuario.
class FTSChatPendingBatchIndex final
{
private:
    using FEntryMap =
        std::unordered_map<std::string, FTSChatPendingBatchEntry>;

public:
    class FPreparedInsert final
    {
    public:
        FPreparedInsert() = default;
        FPreparedInsert(const FPreparedInsert&) = delete;
        FPreparedInsert& operator=(const FPreparedInsert&) = delete;
        FPreparedInsert(FPreparedInsert&&) noexcept = default;
        FPreparedInsert& operator=(FPreparedInsert&&) noexcept = default;

    private:
        friend class FTSChatPendingBatchIndex;
        explicit FPreparedInsert(FEntryMap::node_type Node) noexcept
            : Node(std::move(Node))
        {
        }
        FEntryMap::node_type Node;
    };

    struct FPreparedEraseExact
    {
        std::string UserUniqueId;
        FTSEmissionId EmissionId = 0;
        bool bPending = false;
    };

    struct FPreparedReplaceExact
    {
        std::string UserUniqueId;
        FTSEmissionId ExpectedEmissionId = 0;
        FTSChatPendingBatchEntry Replacement{};
        bool bPending = false;
    };

    FTSChatPendingBatchIndex() = default;
    FTSChatPendingBatchIndex(const FTSChatPendingBatchIndex&) = delete;
    FTSChatPendingBatchIndex& operator=(const FTSChatPendingBatchIndex&) = delete;
    FTSChatPendingBatchIndex(FTSChatPendingBatchIndex&&) = delete;
    FTSChatPendingBatchIndex& operator=(FTSChatPendingBatchIndex&&) = delete;

    template <typename TCallback>
    [[nodiscard]]
    bool VisitByUser(
        const std::string& UserUniqueId,
        TCallback&& Callback
    ) const
    {
        const auto EntryIt = Entries.find(UserUniqueId);
        if (EntryIt == Entries.end())
        {
            return false;
        }

        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const FTSChatPendingBatchEntry&>(EntryIt->second)
        );
        return true;
    }

    template <typename TCallback>
    void VisitAll(TCallback&& Callback) const
    {
        for (const auto& [UserUniqueId, Entry] : Entries)
        {
            std::invoke(
                std::forward<TCallback>(Callback),
                static_cast<const std::string&>(UserUniqueId),
                static_cast<const FTSChatPendingBatchEntry&>(Entry)
            );
        }
    }

    [[nodiscard]]
    bool ContainsExact(
        const std::string& UserUniqueId,
        FTSEmissionId EmissionId
    ) const noexcept;

    [[nodiscard]]
    std::optional<FPreparedInsert> PrepareInsert(
        std::string UserUniqueId,
        FTSChatPendingBatchEntry Entry
    );

    // Prepara el nodo sucesor mientras la entrada expirada aún existe; lifecycle
    // eliminará la identidad exacta antes del commit noexcept del nuevo nodo.
    [[nodiscard]]
    std::optional<FPreparedInsert> PrepareInsertAfterExactErase(
        std::string UserUniqueId,
        FTSEmissionId ExpectedEmissionId,
        FTSChatPendingBatchEntry Entry
    );

    void CommitInsert(FPreparedInsert& Prepared) noexcept;

    [[nodiscard]]
    std::optional<FPreparedEraseExact> PrepareEraseExact(
        const std::string& UserUniqueId,
        FTSEmissionId EmissionId
    ) const;

    void CommitEraseExact(FPreparedEraseExact& Prepared) noexcept;

    [[nodiscard]]
    std::optional<FPreparedReplaceExact> PrepareReplaceExact(
        const std::string& UserUniqueId,
        FTSEmissionId ExpectedEmissionId,
        FTSChatPendingBatchEntry Replacement
    ) const;

    void CommitReplaceExact(FPreparedReplaceExact& Prepared) noexcept;

    [[nodiscard]] std::size_t Size() const noexcept;

private:
    FEntryMap Entries;
};
