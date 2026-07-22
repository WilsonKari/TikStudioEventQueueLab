#include "EventPipeline/State/TSChatPendingBatchIndex.h"

bool FTSChatPendingBatchIndex::ContainsExact(
    const std::string& UserUniqueId,
    FTSEmissionId EmissionId
) const noexcept
{
    const auto EntryIt = Entries.find(UserUniqueId);
    return EntryIt != Entries.end() &&
        EntryIt->second.EmissionId == EmissionId;
}

std::optional<FTSChatPendingBatchIndex::FPreparedInsert>
FTSChatPendingBatchIndex::PrepareInsert(
    std::string UserUniqueId,
    FTSChatPendingBatchEntry Entry
)
{
    if (UserUniqueId.empty() || Entry.EmissionId == 0 ||
        Entries.contains(UserUniqueId))
    {
        return std::nullopt;
    }

    Entries.reserve(Entries.size() + 1);
    FEntryMap Staging;
    const auto [EntryIt, bInserted] = Staging.emplace(
        std::move(UserUniqueId),
        Entry
    );
    if (!bInserted)
    {
        return std::nullopt;
    }

    return FPreparedInsert(Staging.extract(EntryIt));
}

std::optional<FTSChatPendingBatchIndex::FPreparedInsert>
FTSChatPendingBatchIndex::PrepareInsertAfterExactErase(
    std::string UserUniqueId,
    FTSEmissionId ExpectedEmissionId,
    FTSChatPendingBatchEntry Entry
)
{
    if (UserUniqueId.empty() || Entry.EmissionId == 0 ||
        !ContainsExact(UserUniqueId, ExpectedEmissionId))
    {
        return std::nullopt;
    }

    Entries.reserve(Entries.size() + 1);
    FEntryMap Staging;
    const auto [EntryIt, bInserted] = Staging.emplace(
        std::move(UserUniqueId),
        Entry
    );
    if (!bInserted)
    {
        return std::nullopt;
    }

    return FPreparedInsert(Staging.extract(EntryIt));
}

void FTSChatPendingBatchIndex::CommitInsert(
    FPreparedInsert& Prepared
) noexcept
{
    if (Prepared.Node.empty())
    {
        std::terminate();
    }

    if (!Entries.insert(std::move(Prepared.Node)).inserted)
    {
        std::terminate();
    }
}

std::optional<FTSChatPendingBatchIndex::FPreparedEraseExact>
FTSChatPendingBatchIndex::PrepareEraseExact(
    const std::string& UserUniqueId,
    FTSEmissionId EmissionId
) const
{
    if (!ContainsExact(UserUniqueId, EmissionId))
    {
        return std::nullopt;
    }

    return FPreparedEraseExact{UserUniqueId, EmissionId, true};
}

void FTSChatPendingBatchIndex::CommitEraseExact(
    FPreparedEraseExact& Prepared
) noexcept
{
    const auto EntryIt = Entries.find(Prepared.UserUniqueId);
    if (!Prepared.bPending || EntryIt == Entries.end() ||
        EntryIt->second.EmissionId != Prepared.EmissionId)
    {
        std::terminate();
    }

    Entries.erase(EntryIt);
    Prepared.bPending = false;
}

std::optional<FTSChatPendingBatchIndex::FPreparedReplaceExact>
FTSChatPendingBatchIndex::PrepareReplaceExact(
    const std::string& UserUniqueId,
    FTSEmissionId ExpectedEmissionId,
    FTSChatPendingBatchEntry Replacement
) const
{
    if (Replacement.EmissionId == 0 ||
        !ContainsExact(UserUniqueId, ExpectedEmissionId))
    {
        return std::nullopt;
    }

    return FPreparedReplaceExact{
        UserUniqueId,
        ExpectedEmissionId,
        Replacement,
        true
    };
}

void FTSChatPendingBatchIndex::CommitReplaceExact(
    FPreparedReplaceExact& Prepared
) noexcept
{
    const auto EntryIt = Entries.find(Prepared.UserUniqueId);
    if (!Prepared.bPending || EntryIt == Entries.end() ||
        EntryIt->second.EmissionId != Prepared.ExpectedEmissionId)
    {
        std::terminate();
    }

    EntryIt->second = Prepared.Replacement;
    Prepared.bPending = false;
}

std::size_t FTSChatPendingBatchIndex::Size() const noexcept
{
    return Entries.size();
}
