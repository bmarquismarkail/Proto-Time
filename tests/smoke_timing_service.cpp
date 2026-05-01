#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

#include "machine/TimingService.hpp"

#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "check failed: " << #expr << '\n'; \
            return 1; \
        } \
    } while (false)

int main()
{
    using SteadyClock = std::chrono::steady_clock;

    BMMQ::TimingService svc;
    BMMQ::TimingConfig cfg;
    cfg.baseClockHz = 1000000.0; // 1 MHz
    cfg.minInstructionCycles = 4.0;
    cfg.maxCatchUp = std::chrono::milliseconds(8);
    // Use a small sleep quantum for deterministic unit test behavior
    cfg.minSleepQuantum = std::chrono::microseconds(1);
    cfg.throttled = true;
    svc.configure(cfg);
    {
        BMMQ::TimingConfig profileCfg;
        BMMQ::applyTimingPolicyProfileDefaults(BMMQ::TimingPolicyProfile::LowLatency, profileCfg);
        CHECK_TRUE(profileCfg.profile == BMMQ::TimingPolicyProfile::LowLatency);
        CHECK_TRUE(profileCfg.adaptiveSleepEnabled);
        CHECK_TRUE(std::string(BMMQ::timingPolicyProfileName(profileCfg.profile)) == "low_latency");
        CHECK_TRUE(BMMQ::parseTimingPolicyProfile("power_saver") == BMMQ::TimingPolicyProfile::PowerSaver);
    }

    const auto t0 = SteadyClock::now();
    svc.start(t0);

    // Accumulate a little time and ensure execution is permitted.
    svc.update(t0 + std::chrono::milliseconds(1));
    CHECK_TRUE(svc.canExecute());

    // Unthrottled mode allows execution regardless of budget.
    svc.setThrottled(false);
    svc.update(t0 + std::chrono::milliseconds(1));
    CHECK_TRUE(svc.canExecute());
    auto now1 = SteadyClock::now();
    CHECK_TRUE(svc.nextWakeTime(now1) <= now1);

    {
        const auto unthrottledStats = svc.stats();
        CHECK_TRUE(unthrottledStats.configuredMinSleepQuantum == cfg.minSleepQuantum);
    }

    // Speed multiplier affects effective clock rate.
    svc.setSpeedMultiplier(2.0);
    {
        const auto s = svc.stats();
        CHECK_TRUE(std::fabs(s.speedMultiplier - 2.0) < 1e-9);
        CHECK_TRUE(std::fabs(s.effectiveClockHz - cfg.baseClockHz * 2.0) < 1e-3);
    }

    // Pause + single-step semantics.
    svc.setThrottled(true);
    svc.setPaused(true);
    svc.update(now1 + std::chrono::milliseconds(1));
    CHECK_TRUE(!svc.canExecute());
    svc.requestSingleStep();
    CHECK_TRUE(svc.canExecute());
    svc.charge(4.0);
    CHECK_TRUE(!svc.canExecute());
    {
        const auto s2 = svc.stats();
        CHECK_TRUE(s2.singleStepsGranted >= 1);
    }

    {
        BMMQ::TimingService invalidSliceSvc;
        BMMQ::TimingConfig invalidSliceCfg;
        invalidSliceCfg.baseClockHz = 1000000.0;
        invalidSliceCfg.executionSliceSeconds = 0.0;
        invalidSliceCfg.frontendServiceSliceSeconds = -1.0;
        invalidSliceSvc.configure(invalidSliceCfg);
        CHECK_TRUE(invalidSliceSvc.config().executionSliceSeconds > 0.0);
        CHECK_TRUE(invalidSliceSvc.config().frontendServiceSliceSeconds > 0.0);
    }

    // Catch-up clamping on large host delay.
    svc.setPaused(false);
    svc.setSpeedMultiplier(1.0);
    const auto tbig = t0 + std::chrono::milliseconds(200);
    svc.update(tbig);
    {
        const auto s3 = svc.stats();
        const double maxBudget = std::max(cfg.minInstructionCycles,
                                          cfg.baseClockHz * std::chrono::duration<double>(cfg.maxCatchUp).count());
        CHECK_TRUE(s3.cycleBudget <= maxBudget + 1.0);
        CHECK_TRUE(s3.catchUpClampCount >= 1);
    }

    // When budget is exhausted and throttled, nextWakeTime should be in the future.
    const auto s4 = svc.stats();
    svc.charge(s4.cycleBudget);
    const auto now2 = SteadyClock::now();
    const auto nw = svc.nextWakeTime(now2);
    CHECK_TRUE(nw > now2);

    {
        const auto s5 = svc.stats();
        CHECK_TRUE(s5.sleepDecisions >= 1u);
    }

    // With a larger sleep quantum than the remaining timing deficit, wake should be immediate.
    BMMQ::TimingService subQuantumSvc;
    BMMQ::TimingConfig subQuantumCfg;
    subQuantumCfg.baseClockHz = 1000000.0;
    subQuantumCfg.minInstructionCycles = 4.0;
    subQuantumCfg.maxCatchUp = std::chrono::milliseconds(8);
    subQuantumCfg.minSleepQuantum = std::chrono::milliseconds(1);
    subQuantumCfg.throttled = true;
    subQuantumSvc.configure(subQuantumCfg);
    subQuantumSvc.start(t0);
    subQuantumSvc.update(t0 + std::chrono::microseconds(1));
    subQuantumSvc.charge(1.0);
    const auto subNow = t0 + std::chrono::microseconds(1);
    CHECK_TRUE(subQuantumSvc.nextWakeTime(subNow) <= subNow);
    CHECK_TRUE(subQuantumSvc.stats().sleepSkippedForSmallDeficit >= 1u);

    subQuantumSvc.recordWakeBurst(120.0, 3u);
    subQuantumSvc.noteWakeBurstSliceLimitHit();
    subQuantumSvc.noteWakeBurstCycleLimitHit();
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(50), std::chrono::microseconds(75));
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(80), std::chrono::microseconds(70));
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(100), std::chrono::microseconds(150));
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(100), std::chrono::microseconds(450));
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(100), std::chrono::microseconds(1600));
    subQuantumSvc.noteHostSleep(std::chrono::microseconds(100), std::chrono::microseconds(3500));
    subQuantumSvc.noteFrontendServiceTick(5u, 2u, std::chrono::microseconds(1200));
    {
        const auto s6 = subQuantumSvc.stats();
        CHECK_TRUE(s6.wakeBurstSamples >= 1u);
        CHECK_TRUE(s6.wakeBurstCyclesLast >= 120.0);
        CHECK_TRUE(s6.wakeBurstSlicesLast >= 3u);
        CHECK_TRUE(s6.wakeBurstSliceLimitHitCount >= 1u);
        CHECK_TRUE(s6.wakeBurstCycleLimitHitCount >= 1u);
        CHECK_TRUE(s6.sleepCalls >= 1u);
        CHECK_TRUE(s6.sleepOvershootCount >= 1u);
        CHECK_TRUE(s6.sleepOvershootHighWater >= std::chrono::microseconds(25));
        CHECK_TRUE(s6.sleepWakeEarlyCount >= 1u);
        CHECK_TRUE(s6.sleepWakeLateCount >= 1u);
        CHECK_TRUE(s6.sleepWakeJitterUnder100usCount >= 1u);
        CHECK_TRUE(s6.sleepWakeJitter100To500usCount >= 1u);
        CHECK_TRUE(s6.sleepWakeJitter500usTo2msCount >= 1u);
        CHECK_TRUE(s6.sleepWakeJitterOver2msCount >= 1u);
        CHECK_TRUE(s6.sleepWakeLateStreakHighWater >= 1u);
        CHECK_TRUE(s6.frontendTicksScheduled >= 5u);
        CHECK_TRUE(s6.frontendTicksExecuted >= 2u);
        CHECK_TRUE(s6.frontendTicksMerged >= 3u);
        CHECK_TRUE(s6.frontendTickDelayLast >= std::chrono::microseconds(1200));
    }

    return 0;
}

#undef CHECK_TRUE
