#include "EventPipeline/Settings/TSChatSemanticSettings.h"

bool AreChatSemanticSettingsValid(
    const FTSChatSemanticSettings& Settings
) noexcept
{
    if (Settings.CommandPrefix.empty() ||
        Settings.MaxCommandPrefixUtf8Bytes == 0 ||
        Settings.CommandPrefix.size() > Settings.MaxCommandPrefixUtf8Bytes ||
        Settings.MaxMessagesPerBatch == 0 ||
        Settings.MaxMessageUtf8Bytes == 0 ||
        Settings.MaxBatchUtf8Bytes == 0 ||
        Settings.MaxBatchUtf8Bytes < Settings.MaxMessageUtf8Bytes)
    {
        return false;
    }

    for (const unsigned char Character : Settings.CommandPrefix)
    {
        if (Character == 0 || Character <= 0x20 || Character == 0x7f)
        {
            return false;
        }
    }

    return true;
}
