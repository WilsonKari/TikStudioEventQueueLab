#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Límites y clasificación configurables de la semántica acumulativa de Chat.
struct FTSChatSemanticSettings
{
    bool bOnlyAllowCommands = false;
    std::string CommandPrefix = "!";
    bool bAllowLeadingWhitespace = true;
    bool bRequireCommandBoundary = false;
    std::size_t MaxCommandPrefixUtf8Bytes = 16;
    std::uint32_t MaxMessagesPerBatch = 8;
    std::size_t MaxMessageUtf8Bytes = 1024;
    std::size_t MaxBatchUtf8Bytes = 4096;
};

[[nodiscard]]
bool AreChatSemanticSettingsValid(
    const FTSChatSemanticSettings& Settings
) noexcept;
