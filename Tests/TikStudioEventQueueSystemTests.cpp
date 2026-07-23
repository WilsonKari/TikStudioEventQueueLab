#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct FTSEventQueueSystemTestAccess
{
    static bool SetPendingRevision(
        TikStudioEventQueueSystem& Queue,
        FTSEmissionId EmissionId,
        std::uint64_t Revision
    )
    {
        return Queue.SetPendingRevisionForTesting(EmissionId, Revision);
    }
};

namespace
{
    using namespace std::chrono_literals;

    struct FControlledClock
    {
        FTSEventQueueTimePoint Now{};
        std::size_t CaptureCount = 0;
        std::size_t ThrowOnCapture = 0;

        [[nodiscard]]
        FTSNowProvider MakeProvider()
        {
            return [this]()
            {
                ++CaptureCount;
                if (ThrowOnCapture == CaptureCount)
                {
                    throw std::runtime_error("Controlled clock failure");
                }
                return Now;
            };
        }

        template <typename Rep, typename Period>
        void Advance(std::chrono::duration<Rep, Period> Delta)
        {
            Now += std::chrono::duration_cast<FTSEventQueueClock::duration>(
                Delta
            );
        }
    };

    void Require(bool bCondition, const std::string& Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    [[nodiscard]]
    FTSEventQueueSettings MakeSettings(
        bool bPumpAfterEnqueue,
        bool bPumpAfterConfirm
    )
    {
        FTSEventQueueSettings Settings;
        Settings.Pump.bPumpAfterEnqueueWhenIdle = bPumpAfterEnqueue;
        Settings.Pump.bPumpAfterConfirm = bPumpAfterConfirm;
        return Settings;
    }

    FTSFlowQueueSettings& ConfigureFlow(
        FTSEventQueueSettings& Settings,
        ETSEventFlow Flow,
        std::int32_t BaseWeight,
        std::chrono::milliseconds TTL,
        std::uint32_t MaxSlots,
        ETSEventExpirePolicy ExpirePolicy = ETSEventExpirePolicy::Discard
    )
    {
        FTSFlowQueueSettings* FlowSettings =
            Settings.TryGetFlowSettings(Flow);

        Require(FlowSettings != nullptr, "The requested test flow must be valid");

        FlowSettings->bEnabled = true;
        FlowSettings->BaseWeight = BaseWeight;
        FlowSettings->TTL = TTL;
        FlowSettings->ExpirePolicy = ExpirePolicy;
        FlowSettings->MaxSlots = MaxSlots;
        FlowSettings->bExemptFromEviction = false;
        return *FlowSettings;
    }

    [[nodiscard]]
    FTSEnqueueRequest MakeRequest(
        ETSEventFlow Flow,
        std::int64_t PriorityAdjustment = 0
    )
    {
        FTSEnqueueRequest Request;
        Request.Flow = Flow;
        Request.PriorityAdjustment = PriorityAdjustment;
        return Request;
    }

    [[nodiscard]]
    FTSEnqueueRequest MakeRequestWithTTL(
        ETSEventFlow Flow,
        std::int64_t PriorityAdjustment,
        std::chrono::milliseconds TTL
    )
    {
        FTSEnqueueRequest Request = MakeRequest(Flow, PriorityAdjustment);
        Request.bOverrideTTL = true;
        Request.TTLOverride = TTL;
        return Request;
    }

    void RequireAccepted(
        const FTSEnqueueResult& Result,
        ETSEventFlow ExpectedFlow,
        const std::string& Context
    )
    {
        Require(
            Result.Status == ETSEnqueueStatus::Accepted,
            Context + ": expected Accepted"
        );
        Require(
            Result.AdmittedEmission.EmissionId != 0,
            Context + ": expected a non-zero EmissionId"
        );
        Require(
            Result.AdmittedEmission.Flow == ExpectedFlow,
            Context + ": admitted flow mismatch"
        );
    }

    void RequireReadyEmission(
        const FTSPumpOutcome& Outcome,
        const FTSEmissionEnvelope& Expected,
        const std::string& Context
    )
    {
        Require(
            Outcome.Status == ETSPumpStatus::EmissionReady,
            Context + ": expected EmissionReady"
        );
        Require(
            Outcome.ReadyEmission.EmissionId == Expected.EmissionId,
            Context + ": ready EmissionId mismatch"
        );
        Require(
            Outcome.ReadyEmission.Flow == Expected.Flow,
            Context + ": ready flow mismatch"
        );
    }

    void RequireLifecycleEvent(
        const FTSEmissionLifecycleEvent& Event,
        const FTSEmissionEnvelope& Expected,
        ETSEmissionTerminalReason ExpectedReason,
        const std::string& Context
    )
    {
        Require(
            Event.Envelope.EmissionId == Expected.EmissionId,
            Context + ": lifecycle EmissionId mismatch"
        );
        Require(
            Event.Envelope.Flow == Expected.Flow,
            Context + ": lifecycle flow mismatch"
        );
        Require(
            Event.Reason == ExpectedReason,
            Context + ": lifecycle reason mismatch"
        );
    }

    void TestPrioritySelectsHighestScore()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 10)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 50)
        );

        RequireAccepted(A, ETSEventFlow::Chat, "Priority A");
        RequireAccepted(B, ETSEventFlow::Chat, "Priority B");
        Require(
            A.AutoPumpOutcome.Status == ETSPumpStatus::NotRequested
                && B.AutoPumpOutcome.Status == ETSPumpStatus::NotRequested,
            "Priority: Auto Pump must remain disabled"
        );

        const FTSPumpResult PumpResult = Queue.Pump();
        RequireReadyEmission(
            PumpResult.Outcome,
            B.AdmittedEmission,
            "Priority Pump"
        );
        Require(
            PumpResult.LifecycleEvents.empty(),
            "Priority: selection must not emit lifecycle events"
        );
    }

    void TestFIFOForEqualPriority()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 25)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 25)
        );

        RequireAccepted(A, ETSEventFlow::Chat, "FIFO A");
        RequireAccepted(B, ETSEventFlow::Chat, "FIFO B");
        Require(
            A.AdmittedEmission.Sequence < B.AdmittedEmission.Sequence,
            "FIFO: A must have the earlier Sequence"
        );

        const FTSPumpResult PumpResult = Queue.Pump();
        RequireReadyEmission(
            PumpResult.Outcome,
            A.AdmittedEmission,
            "FIFO Pump"
        );
    }

    void TestAutoPumpAfterEnqueue()
    {
        FTSEventQueueSettings Settings = MakeSettings(true, false);
        ConfigureFlow(Settings, ETSEventFlow::Gift, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Gift, 10)
        );

        RequireAccepted(A, ETSEventFlow::Gift, "Auto Enqueue A");
        RequireReadyEmission(
            A.AutoPumpOutcome,
            A.AdmittedEmission,
            "Auto Enqueue"
        );
        Require(
            A.LifecycleEvents.empty(),
            "Auto Enqueue: Pending to InFlight must not emit a terminal"
        );

        const FTSPumpResult BusyResult = Queue.Pump();
        Require(
            BusyResult.Outcome.Status == ETSPumpStatus::Busy,
            "Auto Enqueue: A must remain InFlight"
        );
    }

    void TestEnqueueWhileBusyDoesNotAutoPump()
    {
        FTSEventQueueSettings Settings = MakeSettings(true, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 10)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 20)
        );

        RequireAccepted(A, ETSEventFlow::Chat, "Busy Enqueue A");
        RequireReadyEmission(
            A.AutoPumpOutcome,
            A.AdmittedEmission,
            "Busy Enqueue A Auto Pump"
        );
        RequireAccepted(B, ETSEventFlow::Chat, "Busy Enqueue B");
        Require(
            B.AutoPumpOutcome.Status == ETSPumpStatus::NotRequested,
            "Busy Enqueue: B must not report Busy or run Auto Pump"
        );

        const FTSConfirmResult ConfirmA = Queue.Confirm(
            A.AdmittedEmission.EmissionId
        );
        Require(
            ConfirmA.Status == ETSConfirmStatus::Confirmed,
            "Busy Enqueue: A confirmation failed"
        );
        Require(
            ConfirmA.AutoPumpOutcome.Status == ETSPumpStatus::NotRequested,
            "Busy Enqueue: Confirm Auto Pump must be disabled"
        );

        const FTSPumpResult PumpB = Queue.Pump();
        RequireReadyEmission(
            PumpB.Outcome,
            B.AdmittedEmission,
            "Busy Enqueue B later Pump"
        );
    }

    void TestConfirmAutoPumpsAfterConfirmedLifecycle()
    {
        FTSEventQueueSettings Settings = MakeSettings(true, true);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 10)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 20)
        );
        const FTSEnqueueResult Expiring = Queue.Enqueue(
            MakeRequestWithTTL(ETSEventFlow::Chat, 50, 5s)
        );

        RequireAccepted(A, ETSEventFlow::Chat, "Confirm Auto A");
        RequireReadyEmission(
            A.AutoPumpOutcome,
            A.AdmittedEmission,
            "Confirm Auto initial selection"
        );
        RequireAccepted(B, ETSEventFlow::Chat, "Confirm Auto B");
        RequireAccepted(
            Expiring,
            ETSEventFlow::Chat,
            "Confirm Auto expiring emission"
        );

        Clock.Advance(5s);

        const FTSConfirmResult ConfirmA = Queue.Confirm(
            A.AdmittedEmission.EmissionId
        );

        Require(
            ConfirmA.Status == ETSConfirmStatus::Confirmed,
            "Confirm Auto: expected Confirmed"
        );
        Require(
            ConfirmA.LifecycleEvents.size() == 2,
            "Confirm Auto: expected Confirmed followed by one expiration"
        );
        RequireLifecycleEvent(
            ConfirmA.LifecycleEvents[0],
            A.AdmittedEmission,
            ETSEmissionTerminalReason::Confirmed,
            "Confirm Auto first lifecycle"
        );
        RequireLifecycleEvent(
            ConfirmA.LifecycleEvents[1],
            Expiring.AdmittedEmission,
            ETSEmissionTerminalReason::ExpiredDiscard,
            "Confirm Auto second lifecycle"
        );
        RequireReadyEmission(
            ConfirmA.AutoPumpOutcome,
            B.AdmittedEmission,
            "Confirm Auto next selection"
        );
    }

    void TestCancelDoesNotAutoPump()
    {
        FTSEventQueueSettings Settings = MakeSettings(true, true);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 10)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 20)
        );

        RequireAccepted(A, ETSEventFlow::Chat, "Cancel A");
        RequireAccepted(B, ETSEventFlow::Chat, "Cancel B");

        const FTSCancelInFlightResult CancelA = Queue.CancelInFlight(
            A.AdmittedEmission.EmissionId
        );
        Require(
            CancelA.Status == ETSCancelInFlightStatus::Cancelled,
            "Cancel: expected Cancelled"
        );
        Require(
            CancelA.LifecycleEvents.size() == 1,
            "Cancel: expected exactly one lifecycle event"
        );
        RequireLifecycleEvent(
            CancelA.LifecycleEvents[0],
            A.AdmittedEmission,
            ETSEmissionTerminalReason::Cancelled,
            "Cancel lifecycle"
        );

        const FTSPumpResult PumpB = Queue.Pump();
        RequireReadyEmission(
            PumpB.Outcome,
            B.AdmittedEmission,
            "Cancel subsequent Pump"
        );
    }

    void TestDeterministicExpirationAtTTLBoundary()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(
            Settings,
            ETSEventFlow::Share,
            0,
            5s,
            10,
            ETSEventExpirePolicy::Consolidate
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Share)
        );
        RequireAccepted(A, ETSEventFlow::Share, "Expiration A");

        Clock.Advance(4999ms);
        const FTSProcessDueExpirationsResult BeforeTTL =
            Queue.ProcessDueExpirations();
        Require(
            BeforeTTL.LifecycleEvents.empty(),
            "Expiration: A expired before the TTL boundary"
        );

        Clock.Advance(1ms);
        const FTSProcessDueExpirationsResult AtTTL =
            Queue.ProcessDueExpirations();
        Require(
            AtTTL.LifecycleEvents.size() == 1,
            "Expiration: expected exactly one terminal at the TTL boundary"
        );
        RequireLifecycleEvent(
            AtTTL.LifecycleEvents[0],
            A.AdmittedEmission,
            ETSEmissionTerminalReason::ExpiredConsolidate,
            "Expiration terminal"
        );

        const FTSProcessDueExpirationsResult Repeated =
            Queue.ProcessDueExpirations();
        Require(
            Repeated.LifecycleEvents.empty(),
            "Expiration: the same emission expired more than once"
        );

        const FTSPumpResult EmptyPump = Queue.Pump();
        Require(
            EmptyPump.Outcome.Status == ETSPumpStatus::QueueEmpty,
            "Expiration: expired emission remained selectable"
        );
    }

    void TestInFlightDoesNotExpire()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Follow, 0, 5s, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Follow)
        );
        RequireAccepted(A, ETSEventFlow::Follow, "InFlight TTL A");

        const FTSPumpResult PumpA = Queue.Pump();
        RequireReadyEmission(
            PumpA.Outcome,
            A.AdmittedEmission,
            "InFlight TTL Pump"
        );

        Clock.Advance(10s);
        const FTSProcessDueExpirationsResult Expirations =
            Queue.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.empty(),
            "InFlight TTL: an InFlight emission must not expire"
        );

        const FTSConfirmResult ConfirmA = Queue.Confirm(
            A.AdmittedEmission.EmissionId
        );
        Require(
            ConfirmA.Status == ETSConfirmStatus::Confirmed,
            "InFlight TTL: A must remain confirmable"
        );
        Require(
            ConfirmA.LifecycleEvents.size() == 1,
            "InFlight TTL: expected one Confirmed lifecycle event"
        );
        RequireLifecycleEvent(
            ConfirmA.LifecycleEvents[0],
            A.AdmittedEmission,
            ETSEmissionTerminalReason::Confirmed,
            "InFlight TTL confirmation"
        );
    }

    void TestCapacityCountsPendingAndInFlightAndReleasesSlots()
    {
        {
            FTSEventQueueSettings Settings = MakeSettings(false, false);
            ConfigureFlow(Settings, ETSEventFlow::LikeMilestone, 0, 0ms, 2);

            FControlledClock Clock;
            TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

            const FTSEnqueueResult A = Queue.Enqueue(
                MakeRequest(ETSEventFlow::LikeMilestone)
            );
            const FTSEnqueueResult B = Queue.Enqueue(
                MakeRequest(ETSEventFlow::LikeMilestone)
            );
            const FTSEnqueueResult PendingFull = Queue.Enqueue(
                MakeRequest(ETSEventFlow::LikeMilestone)
            );

            RequireAccepted(A, ETSEventFlow::LikeMilestone, "Capacity A");
            RequireAccepted(B, ETSEventFlow::LikeMilestone, "Capacity B");
            Require(
                PendingFull.Status == ETSEnqueueStatus::RejectedAtCapacity,
                "Capacity: two Pending records must fill both slots"
            );

            const FTSPumpResult PumpA = Queue.Pump();
            RequireReadyEmission(
                PumpA.Outcome,
                A.AdmittedEmission,
                "Capacity Pump A"
            );

            const FTSEnqueueResult PendingPlusInFlightFull = Queue.Enqueue(
                MakeRequest(ETSEventFlow::LikeMilestone)
            );
            Require(
                PendingPlusInFlightFull.Status
                    == ETSEnqueueStatus::RejectedAtCapacity,
                "Capacity: Pending plus InFlight must still fill both slots"
            );

            const FTSConfirmResult ConfirmA = Queue.Confirm(
                A.AdmittedEmission.EmissionId
            );
            Require(
                ConfirmA.Status == ETSConfirmStatus::Confirmed,
                "Capacity: Confirm A failed"
            );

            const FTSEnqueueResult AfterConfirm = Queue.Enqueue(
                MakeRequest(ETSEventFlow::LikeMilestone)
            );
            RequireAccepted(
                AfterConfirm,
                ETSEventFlow::LikeMilestone,
                "Capacity after Confirm"
            );
        }

        {
            FTSEventQueueSettings Settings = MakeSettings(true, false);
            ConfigureFlow(Settings, ETSEventFlow::Member, 0, 0ms, 1);

            FControlledClock Clock;
            TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

            const FTSEnqueueResult A = Queue.Enqueue(
                MakeRequest(ETSEventFlow::Member)
            );
            RequireAccepted(A, ETSEventFlow::Member, "Cancel capacity A");
            RequireReadyEmission(
                A.AutoPumpOutcome,
                A.AdmittedEmission,
                "Cancel capacity initial Auto Pump"
            );

            const FTSEnqueueResult Full = Queue.Enqueue(
                MakeRequest(ETSEventFlow::Member)
            );
            Require(
                Full.Status == ETSEnqueueStatus::RejectedAtCapacity,
                "Capacity: InFlight must occupy the single slot"
            );

            const FTSCancelInFlightResult CancelA = Queue.CancelInFlight(
                A.AdmittedEmission.EmissionId
            );
            Require(
                CancelA.Status == ETSCancelInFlightStatus::Cancelled,
                "Capacity: Cancel A failed"
            );

            const FTSEnqueueResult AfterCancel = Queue.Enqueue(
                MakeRequest(ETSEventFlow::Member)
            );
            RequireAccepted(
                AfterCancel,
                ETSEventFlow::Member,
                "Capacity after Cancel"
            );
        }
    }

    void TestFlowSettingsUpdateAffectsOnlyFutureAdmissions()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& InitialSettings = ConfigureFlow(
            Settings,
            ETSEventFlow::Chat,
            10,
            5s,
            10,
            ETSEventExpirePolicy::Discard
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult BeforeUpdate = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        RequireAccepted(
            BeforeUpdate,
            ETSEventFlow::Chat,
            "Flow update before"
        );
        Require(
            BeforeUpdate.AdmittedEmission.PriorityScore == 10,
            "Flow update: first emission must use the original priority"
        );

        FTSFlowQueueSettings NewSettings = InitialSettings;
        NewSettings.BaseWeight = 50;
        NewSettings.TTL = 20s;
        NewSettings.ExpirePolicy = ETSEventExpirePolicy::Consolidate;
        NewSettings.bExemptFromEviction = true;

        const std::size_t CapturesBeforeUpdate = Clock.CaptureCount;
        const FTSUpdateFlowSettingsResult Update = Queue.UpdateFlowSettings(
            ETSEventFlow::Chat,
            NewSettings
        );
        Require(
            Update.Status == ETSUpdateFlowSettingsStatus::Updated &&
                Update.Flow == ETSEventFlow::Chat,
            "Flow update: valid settings must be accepted"
        );
        Require(
            Clock.CaptureCount == CapturesBeforeUpdate,
            "Flow update must not capture the clock"
        );

        Clock.Advance(1s);
        const FTSEnqueueResult AfterUpdate = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        RequireAccepted(
            AfterUpdate,
            ETSEventFlow::Chat,
            "Flow update after"
        );
        Require(
            AfterUpdate.AdmittedEmission.EmissionId ==
                    BeforeUpdate.AdmittedEmission.EmissionId + 1 &&
                AfterUpdate.AdmittedEmission.PriorityScore == 50,
            "Flow update must not consume identity and must affect later priority"
        );
        Require(
            AfterUpdate.AdmittedEmission.ExpiresAt ==
                Clock.Now + std::chrono::duration_cast<
                    FTSEventQueueClock::duration
                >(20s),
            "Flow update: later emission must use the new TTL"
        );

        const FTSPumpResult Pump = Queue.Pump();
        RequireReadyEmission(
            Pump.Outcome,
            AfterUpdate.AdmittedEmission,
            "Flow update priority selection"
        );

        Clock.Advance(4s);
        const FTSProcessDueExpirationsResult Expirations =
            Queue.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1,
            "Flow update: original Pending emission must keep its TTL"
        );
        RequireLifecycleEvent(
            Expirations.LifecycleEvents.front(),
            BeforeUpdate.AdmittedEmission,
            ETSEmissionTerminalReason::ExpiredDiscard,
            "Flow update original Pending snapshot"
        );
    }

    void TestFlowSettingsUpdatePreservesInFlightSnapshot()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& InitialSettings = ConfigureFlow(
            Settings,
            ETSEventFlow::Follow,
            7,
            5s,
            1
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult Admission = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Follow)
        );
        RequireAccepted(
            Admission,
            ETSEventFlow::Follow,
            "InFlight settings update"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Admission.AdmittedEmission,
            "InFlight settings update Pump"
        );

        FTSFlowQueueSettings NewSettings = InitialSettings;
        NewSettings.bEnabled = false;
        NewSettings.BaseWeight = 100;
        NewSettings.TTL = 1s;
        NewSettings.ExpirePolicy = ETSEventExpirePolicy::Consolidate;
        NewSettings.MaxSlots = 0;
        NewSettings.bExemptFromEviction = true;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Follow,
                NewSettings
            ).Status == ETSUpdateFlowSettingsStatus::Updated,
            "InFlight settings update must succeed"
        );
        Require(
            Queue.Enqueue(MakeRequest(ETSEventFlow::Follow)).Status ==
                ETSEnqueueStatus::RejectedDisabled,
            "Disabled settings must block only the later admission"
        );

        Clock.Advance(10s);
        Require(
            Queue.ProcessDueExpirations().LifecycleEvents.empty(),
            "Settings update must not expire the existing InFlight emission"
        );
        const FTSConfirmResult Confirm = Queue.Confirm(
            Admission.AdmittedEmission.EmissionId
        );
        Require(
            Confirm.Status == ETSConfirmStatus::Confirmed &&
                Confirm.LifecycleEvents.size() == 1,
            "Existing InFlight emission must remain confirmable"
        );
        RequireLifecycleEvent(
            Confirm.LifecycleEvents.front(),
            Admission.AdmittedEmission,
            ETSEmissionTerminalReason::Confirmed,
            "InFlight settings update snapshot"
        );
    }

    void TestInvalidFlowSettingsUpdatesAreAtomic()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& InitialSettings = ConfigureFlow(
            Settings,
            ETSEventFlow::Share,
            17,
            9s,
            3,
            ETSEventExpirePolicy::Discard
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        FTSFlowQueueSettings InvalidSettings = InitialSettings;
        InvalidSettings.BaseWeight = 99;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Count,
                InvalidSettings
            ).Status == ETSUpdateFlowSettingsStatus::RejectedInvalidFlow,
            "Invalid flow update must be rejected"
        );

        InvalidSettings.TTL = -1ms;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Share,
                InvalidSettings
            ).Status == ETSUpdateFlowSettingsStatus::RejectedInvalidTTL,
            "Negative TTL update must be rejected"
        );

        InvalidSettings.TTL = 20s;
        InvalidSettings.ExpirePolicy =
            static_cast<ETSEventExpirePolicy>(255);
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Share,
                InvalidSettings
            ).Status ==
                ETSUpdateFlowSettingsStatus::RejectedInvalidExpirePolicy,
            "Invalid expiration policy update must be rejected"
        );
        Require(
            Clock.CaptureCount == 0,
            "Rejected updates must not capture the clock"
        );

        const FTSEnqueueResult Admission = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Share)
        );
        RequireAccepted(
            Admission,
            ETSEventFlow::Share,
            "Atomic update admission"
        );
        Require(
            Admission.AdmittedEmission.EmissionId == 1 &&
                Admission.AdmittedEmission.PriorityScore == 17 &&
                Admission.AdmittedEmission.ExpiresAt ==
                    Clock.Now + std::chrono::duration_cast<
                        FTSEventQueueClock::duration
                    >(9s),
            "Rejected updates must leave the original settings intact"
        );

        Clock.Advance(9s);
        const FTSProcessDueExpirationsResult Expirations =
            Queue.ProcessDueExpirations();
        Require(
            Expirations.LifecycleEvents.size() == 1,
            "Atomic update emission must expire under original settings"
        );
        RequireLifecycleEvent(
            Expirations.LifecycleEvents.front(),
            Admission.AdmittedEmission,
            ETSEmissionTerminalReason::ExpiredDiscard,
            "Atomic update original policy"
        );
    }

    void TestDisablingFlowPreservesExistingEmission()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& InitialSettings = ConfigureFlow(
            Settings,
            ETSEventFlow::Gift,
            70,
            0ms,
            2
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult Existing = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Gift)
        );
        RequireAccepted(Existing, ETSEventFlow::Gift, "Disable existing");

        FTSFlowQueueSettings DisabledSettings = InitialSettings;
        DisabledSettings.bEnabled = false;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Gift,
                DisabledSettings
            ).Status == ETSUpdateFlowSettingsStatus::Updated,
            "Disable update must succeed"
        );
        Require(
            Queue.Enqueue(MakeRequest(ETSEventFlow::Gift)).Status ==
                ETSEnqueueStatus::RejectedDisabled,
            "Disabled flow must reject later admissions"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Existing.AdmittedEmission,
            "Disabled flow existing emission"
        );
    }

    void TestReducingFlowCapacityDoesNotEvictExistingEmissions()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& InitialSettings = ConfigureFlow(
            Settings,
            ETSEventFlow::Chat,
            0,
            0ms,
            3
        );

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        RequireAccepted(A, ETSEventFlow::Chat, "Reduced capacity A");
        RequireAccepted(B, ETSEventFlow::Chat, "Reduced capacity B");

        FTSFlowQueueSettings ReducedSettings = InitialSettings;
        ReducedSettings.MaxSlots = 1;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Chat,
                ReducedSettings
            ).Status == ETSUpdateFlowSettingsStatus::Updated,
            "Capacity reduction must succeed"
        );
        Require(
            Queue.Enqueue(MakeRequest(ETSEventFlow::Chat)).Status ==
                ETSEnqueueStatus::RejectedAtCapacity,
            "Capacity reduction must reject while existing occupancy exceeds it"
        );

        RequireReadyEmission(
            Queue.Pump().Outcome,
            A.AdmittedEmission,
            "Reduced capacity A remains"
        );
        Require(
            Queue.Confirm(A.AdmittedEmission.EmissionId).Status ==
                ETSConfirmStatus::Confirmed,
            "Reduced capacity A must remain confirmable"
        );
        Require(
            Queue.Enqueue(MakeRequest(ETSEventFlow::Chat)).Status ==
                ETSEnqueueStatus::RejectedAtCapacity,
            "Occupancy equal to the reduced limit must reject"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            B.AdmittedEmission,
            "Reduced capacity B remains"
        );
        Require(
            Queue.Confirm(B.AdmittedEmission.EmissionId).Status ==
                ETSConfirmStatus::Confirmed,
            "Reduced capacity B must remain confirmable"
        );

        ReducedSettings.MaxSlots = 0;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Chat,
                ReducedSettings
            ).Status == ETSUpdateFlowSettingsStatus::Updated,
            "Zero capacity must be a valid settings update"
        );
        Require(
            Queue.Enqueue(MakeRequest(ETSEventFlow::Chat)).Status ==
                ETSEnqueueStatus::RejectedAtCapacity,
            "Zero capacity must reject new admissions"
        );
    }

    void TestAdmissionPrepareFailureLeavesCoreUnchanged()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        bool bThrew = false;
        try
        {
            (void)Queue.EnqueuePrepared(
                MakeRequest(ETSEventFlow::Chat),
                [](const FTSEnqueueResult& PreparedResult)
                {
                    if (PreparedResult.Status != ETSEnqueueStatus::Accepted)
                    {
                        throw std::logic_error(
                            "Admission was not fully prepared"
                        );
                    }
                    throw std::runtime_error(
                        "Controlled external preparation failure"
                    );
                }
            );
        }
        catch (const std::runtime_error&)
        {
            bThrew = true;
        }
        Require(bThrew, "Admission prepare must propagate clock failure");

        const FTSPumpResult EmptyPump = Queue.Pump();
        Require(
            EmptyPump.Outcome.Status == ETSPumpStatus::QueueEmpty,
            "Failed admission prepare must leave no live emission"
        );

        const FTSEnqueueResult Accepted = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        RequireAccepted(Accepted, ETSEventFlow::Chat, "Retry admission");
        Require(
            Accepted.AdmittedEmission.EmissionId == 1 &&
                Accepted.AdmittedEmission.Sequence == 1,
            "Failed prepare must not consume identity or sequence"
        );
    }

    void TestPumpAndConfirmPrepareFailuresPreserveState()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, true);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult Admission = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        RequireAccepted(Admission, ETSEventFlow::Chat, "Prepared failure");

        bool bPumpThrew = false;
        try
        {
            (void)Queue.PumpPrepared(
                [](const FTSPumpResult& PreparedResult)
                {
                    if (PreparedResult.Outcome.Status !=
                        ETSPumpStatus::EmissionReady)
                    {
                        throw std::logic_error("Pump was not fully prepared");
                    }
                    throw std::runtime_error(
                        "Controlled Pump preparation failure"
                    );
                }
            );
        }
        catch (const std::runtime_error&)
        {
            bPumpThrew = true;
        }
        Require(bPumpThrew, "Pump prepare must propagate clock failure");

        const FTSPumpResult Ready = Queue.Pump();
        RequireReadyEmission(
            Ready.Outcome,
            Admission.AdmittedEmission,
            "Pump retry"
        );

        bool bConfirmThrew = false;
        try
        {
            (void)Queue.ConfirmPrepared(
                Admission.AdmittedEmission.EmissionId,
                [](const FTSConfirmResult& PreparedResult)
                {
                    if (PreparedResult.Status != ETSConfirmStatus::Confirmed)
                    {
                        throw std::logic_error(
                            "Confirm was not fully prepared"
                        );
                    }
                    throw std::runtime_error(
                        "Controlled Confirm preparation failure"
                    );
                }
            );
        }
        catch (const std::runtime_error&)
        {
            bConfirmThrew = true;
        }
        Require(bConfirmThrew, "Confirm prepare must propagate clock failure");

        Require(
            Queue.Pump().Outcome.Status == ETSPumpStatus::Busy,
            "Failed Confirm prepare must preserve InFlight"
        );
        const FTSConfirmResult Confirmed = Queue.Confirm(
            Admission.AdmittedEmission.EmissionId
        );
        Require(
            Confirmed.Status == ETSConfirmStatus::Confirmed &&
                Confirmed.LifecycleEvents.size() == 1,
            "Confirm retry must complete exactly once"
        );
    }

    void TestEqualPriorityUsesGlobalSequenceAcrossFlows()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 10, 0ms, 10);
        ConfigureFlow(Settings, ETSEventFlow::Gift, 10, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult Chat = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        const FTSEnqueueResult Gift = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Gift)
        );

        RequireReadyEmission(
            Queue.Pump().Outcome,
            Chat.AdmittedEmission,
            "Cross-flow FIFO first"
        );
        (void)Queue.Confirm(Chat.AdmittedEmission.EmissionId);
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Gift.AdmittedEmission,
            "Cross-flow FIFO second"
        );
    }

    void TestEqualExpirationsAndStaleIndexesRemainDeterministic()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 10ms, 10);
        ConfigureFlow(Settings, ETSEventFlow::Gift, 0, 10ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Gift)
        );
        const FTSEnqueueResult C = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat)
        );

        RequireReadyEmission(
            Queue.Pump().Outcome,
            A.AdmittedEmission,
            "Stale expiration setup"
        );
        (void)Queue.CancelInFlight(A.AdmittedEmission.EmissionId);

        Clock.Advance(10ms);
        const FTSProcessDueExpirationsResult Expired =
            Queue.ProcessDueExpirations();
        Require(
            Expired.LifecycleEvents.size() == 2 &&
                Expired.LifecycleEvents[0].Envelope.EmissionId ==
                    B.AdmittedEmission.EmissionId &&
                Expired.LifecycleEvents[1].Envelope.EmissionId ==
                    C.AdmittedEmission.EmissionId,
            "Equal ExpiresAt must use Sequence after ignoring stale entries"
        );
        Require(
            Queue.Pump().Outcome.Status == ETSPumpStatus::QueueEmpty,
            "Stale priority entries must not resurrect terminal emissions"
        );
    }

    void TestRepeatedScenarioProducesIdenticalIdentityAndLifecycleOrder()
    {
        const auto RunScenario = []()
        {
            FTSEventQueueSettings Settings = MakeSettings(false, false);
            ConfigureFlow(Settings, ETSEventFlow::Chat, 0, 5ms, 10);
            ConfigureFlow(Settings, ETSEventFlow::Gift, 0, 5ms, 10);

            FControlledClock Clock;
            TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
            const FTSEnqueueResult A = Queue.Enqueue(
                MakeRequest(ETSEventFlow::Chat)
            );
            const FTSEnqueueResult B = Queue.Enqueue(
                MakeRequest(ETSEventFlow::Gift)
            );
            (void)Queue.Pump();
            const FTSCancelInFlightResult Cancelled =
                Queue.CancelInFlight(A.AdmittedEmission.EmissionId);
            Clock.Advance(5ms);
            const FTSProcessDueExpirationsResult Expired =
                Queue.ProcessDueExpirations();

            Require(
                Cancelled.Status == ETSCancelInFlightStatus::Cancelled &&
                    Cancelled.LifecycleEvents.size() == 1 &&
                    Expired.LifecycleEvents.size() == 1,
                "Deterministic replay must produce both terminal events"
            );

            std::vector<std::uint64_t> Trace{
                A.AdmittedEmission.EmissionId,
                A.AdmittedEmission.Sequence,
                B.AdmittedEmission.EmissionId,
                B.AdmittedEmission.Sequence,
                Cancelled.LifecycleEvents.front().Envelope.EmissionId,
                static_cast<std::uint64_t>(
                    Cancelled.LifecycleEvents.front().Reason
                ),
                Expired.LifecycleEvents.front().Envelope.EmissionId,
                static_cast<std::uint64_t>(
                    Expired.LifecycleEvents.front().Reason
                )
            };
            return Trace;
        };

        Require(
            RunScenario() == RunScenario(),
            "Repeated deterministic scenarios must produce identical traces"
        );
    }

    void TestConfirmWrongIdPreservesInFlight()
    {
        FTSEventQueueSettings Settings = MakeSettings(true, false);
        ConfigureFlow(Settings, ETSEventFlow::GiftCombo, 0, 0ms, 10);

        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());

        const FTSEnqueueResult A = Queue.Enqueue(
            MakeRequest(ETSEventFlow::GiftCombo)
        );
        RequireAccepted(A, ETSEventFlow::GiftCombo, "Mismatch A");
        RequireReadyEmission(
            A.AutoPumpOutcome,
            A.AdmittedEmission,
            "Mismatch initial Auto Pump"
        );

        const FTSEmissionId WrongId = A.AdmittedEmission.EmissionId + 1;
        const FTSConfirmResult WrongConfirm = Queue.Confirm(WrongId);
        Require(
            WrongConfirm.Status == ETSConfirmStatus::EmissionIdMismatch,
            "Mismatch: expected EmissionIdMismatch"
        );
        Require(
            WrongConfirm.LifecycleEvents.empty(),
            "Mismatch: failed Confirm must not emit lifecycle events"
        );
        Require(
            WrongConfirm.AutoPumpOutcome.Status == ETSPumpStatus::NotRequested,
            "Mismatch: failed Confirm must not request Auto Pump"
        );

        const FTSPumpResult BusyResult = Queue.Pump();
        Require(
            BusyResult.Outcome.Status == ETSPumpStatus::Busy,
            "Mismatch: the real emission must remain InFlight"
        );

        const FTSConfirmResult RealConfirm = Queue.Confirm(
            A.AdmittedEmission.EmissionId
        );
        Require(
            RealConfirm.Status == ETSConfirmStatus::Confirmed,
            "Mismatch: the real emission must remain confirmable"
        );
        Require(
            RealConfirm.LifecycleEvents.size() == 1,
            "Mismatch: real Confirm must emit exactly one terminal"
        );
        RequireLifecycleEvent(
            RealConfirm.LifecycleEvents[0],
            A.AdmittedEmission,
            ETSEmissionTerminalReason::Confirmed,
            "Mismatch real confirmation"
        );
    }

    void TestSchedulingRefreshUsesFrozenTtl()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        FTSFlowQueueSettings& Chat = ConfigureFlow(
            Settings,
            ETSEventFlow::Chat,
            20,
            10s,
            10
        );
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        RequireAccepted(A, ETSEventFlow::Chat, "Frozen TTL A");

        FTSFlowQueueSettings UpdatedSettings = Chat;
        UpdatedSettings.TTL = 30s;
        Require(
            Queue.UpdateFlowSettings(
                ETSEventFlow::Chat,
                UpdatedSettings
            ).Status == ETSUpdateFlowSettingsStatus::Updated,
            "Frozen TTL settings update"
        );

        Clock.Advance(4s);
        FTSUpdatePendingSchedulingRequest Request;
        Request.EmissionId = A.AdmittedEmission.EmissionId;
        Request.ExpectedExpiresAt = A.AdmittedEmission.ExpiresAt;
        Request.bRefreshExpiration = true;
        const FTSUpdatePendingSchedulingResult Refreshed =
            Queue.UpdatePendingScheduling(Request);
        const FTSEventQueueTimePoint ExpectedRefreshedExpiry =
            Clock.Now + std::chrono::duration_cast<
                FTSEventQueueClock::duration
            >(10s);
        Require(
            Refreshed.Status == ETSUpdatePendingSchedulingStatus::Updated &&
                Refreshed.PreviousEmission.ExpiresAt ==
                    A.AdmittedEmission.ExpiresAt &&
                Refreshed.UpdatedEmission.ExpiresAt ==
                    ExpectedRefreshedExpiry &&
                Refreshed.UpdatedEmission.PriorityScore ==
                    A.AdmittedEmission.PriorityScore,
            "Refresh must use the TTL snapshot admitted with A"
        );
        Require(
            Queue.GetNextWakeTime().WakeTime == ExpectedRefreshedExpiry,
            "Refresh must publish the renewed wake"
        );
        bool bVisitedRefreshed = false;
        Require(
            Queue.VisitLiveEmission(
                A.AdmittedEmission.EmissionId,
                [&](const FTSEmissionEnvelope& Emission)
                {
                    bVisitedRefreshed =
                        Emission.ExpiresAt == ExpectedRefreshedExpiry;
                }
            ) &&
                bVisitedRefreshed &&
                Queue.IsPendingEmission(A.AdmittedEmission.EmissionId),
            "Read-only Core queries must expose the refreshed Pending snapshot"
        );

        const FTSEnqueueResult B = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        Require(
            B.AdmittedEmission.ExpiresAt ==
                Clock.Now + std::chrono::duration_cast<
                    FTSEventQueueClock::duration
                >(30s),
            "Later admissions must use the updated TTL"
        );
    }

    void TestSchedulingWithoutExpirationIsUnchanged()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 20, 0ms, 10);
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        Require(
            FTSEventQueueSystemTestAccess::SetPendingRevision(
                Queue,
                A.AdmittedEmission.EmissionId,
                std::numeric_limits<std::uint64_t>::max() - 1
            ),
            "Unchanged revision fixture"
        );

        FTSUpdatePendingSchedulingRequest Request;
        Request.EmissionId = A.AdmittedEmission.EmissionId;
        Request.ExpectedExpiresAt = FTSEventQueueTimePoint::max();
        Request.bRefreshExpiration = true;
        Request.NewPriorityScore = A.AdmittedEmission.PriorityScore;
        const FTSUpdatePendingSchedulingResult Result =
            Queue.UpdatePendingScheduling(Request);
        Require(
            Result.Status == ETSUpdatePendingSchedulingStatus::Unchanged &&
                Result.PreviousEmission.ExpiresAt ==
                    FTSEventQueueTimePoint::max() &&
                Result.UpdatedEmission.PriorityScore ==
                    A.AdmittedEmission.PriorityScore,
            "No-expiration refresh with the same score must be unchanged"
        );
        Require(
            Queue.GetNextWakeTime().Status ==
                ETSNextWakeStatus::NoWakeScheduled,
            "Unchanged no-expiration scheduling must not create a wake"
        );
        FTSUpdatePendingSchedulingRequest ChangePriority;
        ChangePriority.EmissionId = A.AdmittedEmission.EmissionId;
        ChangePriority.NewPriorityScore = 0;
        const FTSUpdatePendingSchedulingResult Changed =
            Queue.UpdatePendingScheduling(ChangePriority);
        Require(
            Changed.Status == ETSUpdatePendingSchedulingStatus::Updated,
            "Unchanged must not consume the final available revision"
        );
        ChangePriority.NewPriorityScore = 1;
        Require(
            Queue.UpdatePendingScheduling(ChangePriority).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedRevisionExhausted,
            "The following real change must observe the exhausted revision"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Changed.UpdatedEmission,
            "Unchanged scheduling priority key"
        );
    }

    void TestTtlOnlySchedulingUpdateRepublishesPriority()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 20, 10s, 10);
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequest(ETSEventFlow::Chat, 10)
        );

        Clock.Advance(5s);
        FTSUpdatePendingSchedulingRequest Request;
        Request.EmissionId = A.AdmittedEmission.EmissionId;
        Request.ExpectedExpiresAt = A.AdmittedEmission.ExpiresAt;
        Request.bRefreshExpiration = true;
        bool bPrepareThrew = false;
        try
        {
            (void)Queue.UpdatePendingSchedulingPrepared(
                Request,
                Clock.Now,
                [](const FTSUpdatePendingSchedulingResult&)
                {
                    throw std::runtime_error("Prepared scheduling failure");
                }
            );
        }
        catch (const std::runtime_error&)
        {
            bPrepareThrew = true;
        }
        Require(
            bPrepareThrew &&
                Queue.GetNextWakeTime().WakeTime ==
                    A.AdmittedEmission.ExpiresAt,
            "Scheduling callback failure must preserve Core"
        );
        const FTSUpdatePendingSchedulingResult Updated =
            Queue.UpdatePendingScheduling(Request);
        Require(
            Updated.Status == ETSUpdatePendingSchedulingStatus::Updated &&
                Updated.UpdatedEmission.ExpiresAt >
                    Updated.PreviousEmission.ExpiresAt,
            "TTL-only scheduling update"
        );

        RequireReadyEmission(
            Queue.Pump().Outcome,
            B.AdmittedEmission,
            "TTL-only update keeps higher priority B"
        );
        (void)Queue.Confirm(B.AdmittedEmission.EmissionId);
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Updated.UpdatedEmission,
            "TTL-only update republishes A priority key"
        );
    }

    void TestPrioritySchedulingUpdateRepublishesExpiration()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 20, 10s, 10);
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        const FTSEnqueueResult B = Queue.Enqueue(
            MakeRequestWithTTL(ETSEventFlow::Chat, 10, 20s)
        );

        FTSUpdatePendingSchedulingRequest RaiseA;
        RaiseA.EmissionId = A.AdmittedEmission.EmissionId;
        RaiseA.ExpectedExpiresAt = A.AdmittedEmission.ExpiresAt;
        RaiseA.NewPriorityScore = 40;
        const FTSUpdatePendingSchedulingResult Raised =
            Queue.UpdatePendingScheduling(RaiseA);
        Require(
            Raised.Status == ETSUpdatePendingSchedulingStatus::Updated &&
                Raised.UpdatedEmission.PriorityScore == 40 &&
                Raised.UpdatedEmission.ExpiresAt ==
                    A.AdmittedEmission.ExpiresAt,
            "Absolute priority update must preserve expiration"
        );
        Require(
            Queue.GetNextWakeTime().WakeTime == A.AdmittedEmission.ExpiresAt,
            "Priority-only update must republish the expiration key"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            Raised.UpdatedEmission,
            "Raised A must outrank B"
        );
        (void)Queue.CancelInFlight(A.AdmittedEmission.EmissionId);

        FTSUpdatePendingSchedulingRequest ZeroB;
        ZeroB.EmissionId = B.AdmittedEmission.EmissionId;
        ZeroB.NewPriorityScore = 0;
        Require(
            Queue.UpdatePendingScheduling(ZeroB).Status ==
                ETSUpdatePendingSchedulingStatus::Updated,
            "Zero must be a valid absolute priority"
        );
        ZeroB.NewPriorityScore = -1;
        Require(
            Queue.UpdatePendingScheduling(ZeroB).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedInvalidRequest,
            "Negative absolute priority must be rejected"
        );
    }

    void TestSchedulingRevisionExhaustionIsAtomic()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 20, 10s, 10);
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        Require(
            FTSEventQueueSystemTestAccess::SetPendingRevision(
                Queue,
                A.AdmittedEmission.EmissionId,
                std::numeric_limits<std::uint64_t>::max()
            ),
            "Revision exhaustion fixture"
        );

        FTSUpdatePendingSchedulingRequest Request;
        Request.EmissionId = A.AdmittedEmission.EmissionId;
        Request.NewPriorityScore = 40;
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedRevisionExhausted,
            "Exhausted revision must reject without wrap"
        );
        Require(
            Queue.GetNextWakeTime().WakeTime == A.AdmittedEmission.ExpiresAt,
            "Exhausted revision must preserve the expiration index"
        );
        RequireReadyEmission(
            Queue.Pump().Outcome,
            A.AdmittedEmission,
            "Exhausted revision must preserve the priority index"
        );
    }

    void TestSchedulingRejectionsDoNotReviveExpiredOrInFlight()
    {
        FTSEventQueueSettings Settings = MakeSettings(false, false);
        ConfigureFlow(Settings, ETSEventFlow::Chat, 20, 5s, 10);
        FControlledClock Clock;
        TikStudioEventQueueSystem Queue(Settings, Clock.MakeProvider());
        const FTSEnqueueResult A = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));

        FTSUpdatePendingSchedulingRequest Request;
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedInvalidRequest,
            "Zero scheduling identity must be invalid"
        );
        Request.EmissionId = A.AdmittedEmission.EmissionId;
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedInvalidRequest,
            "Scheduling request without an operation must be invalid"
        );
        Request.ExpectedExpiresAt = A.AdmittedEmission.ExpiresAt +
            std::chrono::duration_cast<FTSEventQueueClock::duration>(1s);
        Request.bRefreshExpiration = true;
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedExpectationMismatch,
            "Mismatched expiration expectation"
        );

        Clock.Advance(5s);
        Request.ExpectedExpiresAt = A.AdmittedEmission.ExpiresAt;
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedExpired,
            "Expired pending emission must not be revived"
        );
        const FTSProcessDueExpirationsResult Expired =
            Queue.ProcessDueExpirations();
        Require(
            Expired.LifecycleEvents.size() == 1 &&
                Expired.LifecycleEvents.front().Envelope.EmissionId ==
                    A.AdmittedEmission.EmissionId,
            "Expiration maintenance remains the terminal authority"
        );

        const FTSEnqueueResult B = Queue.Enqueue(MakeRequest(ETSEventFlow::Chat));
        RequireReadyEmission(
            Queue.Pump().Outcome,
            B.AdmittedEmission,
            "InFlight rejection setup"
        );
        Request.EmissionId = B.AdmittedEmission.EmissionId;
        Request.ExpectedExpiresAt.reset();
        Require(
            Queue.UpdatePendingScheduling(Request).Status ==
                ETSUpdatePendingSchedulingStatus::RejectedNotPending,
            "InFlight scheduling must be rejected"
        );
        Require(
            !Queue.IsPendingEmission(B.AdmittedEmission.EmissionId),
            "The rejected target must remain InFlight"
        );
    }

    using FTestFunction = void (*)();

    struct FTestCase
    {
        const char* Name = nullptr;
        FTestFunction Function = nullptr;
    };
}

int main()
{
    const std::vector<FTestCase> Tests{
        {"Priority selects highest score", &TestPrioritySelectsHighestScore},
        {"FIFO preserves admission order", &TestFIFOForEqualPriority},
        {"Auto Pump runs after Enqueue", &TestAutoPumpAfterEnqueue},
        {"Busy Enqueue does not Auto Pump", &TestEnqueueWhileBusyDoesNotAutoPump},
        {"Confirm Auto Pumps after lifecycle", &TestConfirmAutoPumpsAfterConfirmedLifecycle},
        {"Cancel does not Auto Pump", &TestCancelDoesNotAutoPump},
        {"TTL expires exactly at boundary", &TestDeterministicExpirationAtTTLBoundary},
        {"InFlight does not expire", &TestInFlightDoesNotExpire},
        {"Capacity counts live records", &TestCapacityCountsPendingAndInFlightAndReleasesSlots},
        {"Flow settings update affects only future admissions", &TestFlowSettingsUpdateAffectsOnlyFutureAdmissions},
        {"Flow settings update preserves InFlight snapshot", &TestFlowSettingsUpdatePreservesInFlightSnapshot},
        {"Invalid flow settings updates are atomic", &TestInvalidFlowSettingsUpdatesAreAtomic},
        {"Disabling a flow preserves existing emissions", &TestDisablingFlowPreservesExistingEmission},
        {"Reducing flow capacity does not evict", &TestReducingFlowCapacityDoesNotEvictExistingEmissions},
        {"Admission prepare failure preserves Core", &TestAdmissionPrepareFailureLeavesCoreUnchanged},
        {"Pump and Confirm prepare failures preserve state", &TestPumpAndConfirmPrepareFailuresPreserveState},
        {"Equal priority uses global cross-flow Sequence", &TestEqualPriorityUsesGlobalSequenceAcrossFlows},
        {"Equal expirations ignore stale indexes deterministically", &TestEqualExpirationsAndStaleIndexesRemainDeterministic},
        {"Repeated scenario produces identical trace", &TestRepeatedScenarioProducesIdenticalIdentityAndLifecycleOrder},
        {"Wrong Confirm ID preserves InFlight", &TestConfirmWrongIdPreservesInFlight},
        {"Scheduling refresh uses frozen TTL", &TestSchedulingRefreshUsesFrozenTtl},
        {"No-expiration scheduling is unchanged", &TestSchedulingWithoutExpirationIsUnchanged},
        {"TTL-only scheduling republishes priority", &TestTtlOnlySchedulingUpdateRepublishesPriority},
        {"Priority scheduling republishes expiration", &TestPrioritySchedulingUpdateRepublishesExpiration},
        {"Scheduling revision exhaustion is atomic", &TestSchedulingRevisionExhaustionIsAtomic},
        {"Scheduling rejections preserve authority", &TestSchedulingRejectionsDoNotReviveExpiredOrInFlight}
    };

    std::size_t PassedCount = 0;
    std::size_t FailedCount = 0;

    for (const FTestCase& Test : Tests)
    {
        try
        {
            Test.Function();
            ++PassedCount;
            std::cout << "PASS: " << Test.Name << '\n';
        }
        catch (const std::exception& Error)
        {
            ++FailedCount;
            std::cerr
                << "FAIL: " << Test.Name
                << " - " << Error.what()
                << '\n';
        }
        catch (...)
        {
            ++FailedCount;
            std::cerr
                << "FAIL: " << Test.Name
                << " - unknown exception"
                << '\n';
        }
    }

    std::cout
        << "RESULT: " << PassedCount << " passed, "
        << FailedCount << " failed\n";

    return FailedCount == 0 ? 0 : 1;
}
