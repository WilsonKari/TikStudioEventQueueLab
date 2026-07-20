#include "EventQueueSystem/TikStudioEventQueueSystem.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    struct FControlledClock
    {
        FTSEventQueueTimePoint Now{};

        [[nodiscard]]
        FTSNowProvider MakeProvider()
        {
            return [this]()
            {
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
        {"Wrong Confirm ID preserves InFlight", &TestConfirmWrongIdPreservesInFlight}
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
