#include "EventQueueSystem/TSEventQueueSystemSettings.h"

FTSEventQueueSettings::FTSEventQueueSettings()
{
    GetFlowSettings(ETSEventFlow::Chat) = FTSFlowQueueSettings{
        true, 40, std::chrono::milliseconds{8000},
        ETSEventExpirePolicy::Discard, 30, false
    };

    GetFlowSettings(ETSEventFlow::Gift) = FTSFlowQueueSettings{
        true, 70, std::chrono::milliseconds{45000},
        ETSEventExpirePolicy::Discard, 1000, false
    };

    GetFlowSettings(ETSEventFlow::GiftCombo) = FTSFlowQueueSettings{
        true, 80, std::chrono::milliseconds{60000},
        ETSEventExpirePolicy::Discard, 1000, false
    };

    GetFlowSettings(ETSEventFlow::Follow) = FTSFlowQueueSettings{
        true, 60, std::chrono::milliseconds{30000},
        ETSEventExpirePolicy::Discard, 10, false
    };

    GetFlowSettings(ETSEventFlow::Like) = FTSFlowQueueSettings{
        true, 25, std::chrono::milliseconds{10000},
        ETSEventExpirePolicy::Discard, 1, false
    };

    GetFlowSettings(ETSEventFlow::LikeUser) = FTSFlowQueueSettings{
        true, 10, std::chrono::milliseconds{5000},
        ETSEventExpirePolicy::Discard, 5, false
    };

    GetFlowSettings(ETSEventFlow::MemberIdentity) = FTSFlowQueueSettings{
        true, 5, std::chrono::milliseconds{6000},
        ETSEventExpirePolicy::Discard, 10, false
    };

    GetFlowSettings(ETSEventFlow::MemberNormalized) = FTSFlowQueueSettings{
        true, 20, std::chrono::milliseconds{12000},
        ETSEventExpirePolicy::Discard, 1, false
    };

    GetFlowSettings(ETSEventFlow::RoomUser) = FTSFlowQueueSettings{
        true, 35, std::chrono::milliseconds{15000},
        ETSEventExpirePolicy::Discard, 1, false
    };

    GetFlowSettings(ETSEventFlow::RoomUserMilestone) = FTSFlowQueueSettings{
        true, 30, std::chrono::milliseconds{8000},
        ETSEventExpirePolicy::Discard, 1, false
    };

    GetFlowSettings(ETSEventFlow::RoomUserTop1Change) = FTSFlowQueueSettings{
        true, 50, std::chrono::milliseconds{10000},
        ETSEventExpirePolicy::Discard, 2, false
    };

    GetFlowSettings(ETSEventFlow::Share) = FTSFlowQueueSettings{
        true, 55, std::chrono::milliseconds{25000},
        ETSEventExpirePolicy::Discard, 10, false
    };

    GetFlowSettings(ETSEventFlow::ShareMilestone) = FTSFlowQueueSettings{
        true, 50, std::chrono::milliseconds{15000},
        ETSEventExpirePolicy::Discard, 1, false
    };

    Eviction.bEnableCompetitiveEviction = false;
    Eviction.bTrackEvictionMetrics = false;

    Fairness.AgingPointsPerSecond = 0.0;
    Fairness.AgingMaxBonus = 20;

    Pump.bPumpOnFirstEnqueue = true;
    Pump.bPumpAfterConfirm = true;
    Pump.bRecomputeOnPump = true;
}
