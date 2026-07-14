#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <memory>
#include <utility>

class TikStudioEventQueueSystem::FImpl final
{
public:
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

    FTSEventQueueSettings Settings;
    FTSNowProvider NowProvider;
};

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

TikStudioEventQueueSystem::~TikStudioEventQueueSystem() = default;

TikStudioEventQueueSystem::TikStudioEventQueueSystem(
    TikStudioEventQueueSystem&&
) noexcept = default;

TikStudioEventQueueSystem& TikStudioEventQueueSystem::operator=(
    TikStudioEventQueueSystem&&
) noexcept = default;
