#include <algorithm>
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

using SteadyClock = std::chrono::steady_clock;

int main()
{
    BMMQ::TimingConfig cfg;
    cfg.baseClockHz = 1000000.0; // 1 MHz synthetic
    cfg.minInstructionCycles = 4.0;
    cfg.maxCatchUp = std::chrono::milliseconds(8);
    cfg.minSleepQuantum = std::chrono::microseconds(10);
    cfg.throttled = true;
    cfg.maxExecutionSlicesPerWake = 0u;
    cfg.maxCyclesPerWake = 0.0;
    cfg.sleepSpinWindow = std::chrono::microseconds(400);
    cfg.sleepSpinCap = std::chrono::microseconds(200);

    BMMQ::TimingEngine engine(cfg);
    CHECK_TRUE(engine.config().maxExecutionSlicesPerWake >= 1u);
    CHECK_TRUE(engine.config().maxCyclesPerWake >= engine.config().minInstructionCycles);
    CHECK_TRUE(engine.config().sleepSpinWindow <= engine.config().sleepSpinCap);

    const auto t0 = SteadyClock::now();
    engine.start(t0);

    // Advance by 1ms -> should accumulate ~1000 cycles and be eligible to run
    engine.update(t0 + std::chrono::milliseconds(1));
    CHECK_TRUE(engine.canExecute());

    // Charge a small retired cycle count; engine should charge at least minInstructionCycles
    engine.charge(1.0);
    // After charging minInstructionCycles, budget remains non-negative
    {
        const auto s = engine.stats();
        CHECK_TRUE(s.cycleBudget >= 0.0);
    }

    // Large update should clamp to maxCatchUp window
    engine.update(t0 + std::chrono::milliseconds(200));
    {
        const auto s = engine.stats();
        // With 1MHz and 8ms catch-up, max budget should be >= 8000 cycles
        CHECK_TRUE(s.cycleBudget <= std::max(4.0, cfg.baseClockHz * std::chrono::duration<double>(cfg.maxCatchUp).count()) + 1.0);
    }

    // Sub-quantum deficits should not trigger sleeping.
    BMMQ::TimingConfig tinySleepCfg;
    tinySleepCfg.baseClockHz = 1000000.0;
    tinySleepCfg.minInstructionCycles = 4.0;
    tinySleepCfg.minSleepQuantum = std::chrono::microseconds(10);
    tinySleepCfg.throttled = true;
    BMMQ::TimingEngine tinySleepEngine(tinySleepCfg);
    tinySleepEngine.start(t0);
    tinySleepEngine.update(t0 + std::chrono::microseconds(1));
    tinySleepEngine.charge(1.0);
    const auto subQuantumNow = t0 + std::chrono::microseconds(1);
    CHECK_TRUE(!tinySleepEngine.shouldSleep(subQuantumNow));
    CHECK_TRUE(tinySleepEngine.nextWakeTime(subQuantumNow) <= subQuantumNow);
    CHECK_TRUE(tinySleepEngine.stats().sleepSkippedForSmallDeficit >= 1u);

    // Instruction overshoot must become timing debt. Otherwise an instruction
    // that costs more than the current budget makes emulation run fast.
    BMMQ::TimingConfig overshootCfg;
    overshootCfg.baseClockHz = 1000000.0;
    overshootCfg.minInstructionCycles = 4.0;
    overshootCfg.minSleepQuantum = std::chrono::microseconds(1);
    overshootCfg.throttled = true;
    BMMQ::TimingEngine overshootEngine(overshootCfg);
    overshootEngine.start(t0);
    overshootEngine.update(t0 + std::chrono::microseconds(4));
    CHECK_TRUE(overshootEngine.canExecute());
    overshootEngine.charge(16.0);
    CHECK_TRUE(!overshootEngine.canExecute());
    CHECK_TRUE(overshootEngine.stats().cycleDebt >= 12.0);
    overshootEngine.update(t0 + std::chrono::microseconds(19));
    CHECK_TRUE(!overshootEngine.canExecute());
    overshootEngine.update(t0 + std::chrono::microseconds(20));
    CHECK_TRUE(overshootEngine.canExecute());

    // Scheduler-sized deficits should allow sleeping.
    BMMQ::TimingConfig sleepCfg;
    sleepCfg.baseClockHz = 1000.0;
    sleepCfg.minInstructionCycles = 4.0;
    sleepCfg.minSleepQuantum = std::chrono::microseconds(10);
    sleepCfg.throttled = true;
    BMMQ::TimingEngine sleepEngine(sleepCfg);
    sleepEngine.start(t0);
    const auto sleepyNow = t0 + std::chrono::microseconds(1);
    CHECK_TRUE(sleepEngine.shouldSleep(sleepyNow));
    CHECK_TRUE(sleepEngine.nextWakeTime(sleepyNow) > sleepyNow);
    CHECK_TRUE(sleepEngine.stats().sleepDecisions >= 1u);

    // Paused mode is applied as an outer-loop control snapshot, not by service calls
    // inside the instruction loop.
    BMMQ::TimingControlState paused;
    paused.paused = true;
    engine.applyControl(paused);
    engine.update(t0 + std::chrono::milliseconds(300));
    CHECK_TRUE(!engine.canExecute());
    CHECK_TRUE(!engine.shouldSleep(t0 + std::chrono::milliseconds(300)));
    paused.singleStepRequested = true;
    engine.applyControl(paused);
    CHECK_TRUE(engine.canExecute());
    engine.charge(4.0); // consume single step
    CHECK_TRUE(!engine.canExecute());
    CHECK_TRUE(engine.stats().singleStepsGranted == 1u);

    // Execution slicing should be cycle-based and default to roughly 1 ms of emulated time.
    BMMQ::TimingConfig sliceCfg;
    sliceCfg.baseClockHz = 1000000.0;
    sliceCfg.minInstructionCycles = 4.0;
    sliceCfg.executionSliceSeconds = 0.001;
    sliceCfg.frontendServiceSliceSeconds = 0.001;
    BMMQ::TimingEngine sliceEngine(sliceCfg);
    sliceEngine.start(t0);

    sliceEngine.beginExecutionSlice();
    CHECK_TRUE(sliceEngine.stats().executionSlicesEntered == 1u);
    auto sliceDecision = sliceEngine.recordExecutionSliceCycles(996.0);
    CHECK_TRUE(!sliceDecision.executionSliceComplete);
    CHECK_TRUE(!sliceDecision.frontendServiceDue);
    CHECK_TRUE(sliceEngine.stats().frontendServiceChecks == 0u);

    sliceDecision = sliceEngine.recordExecutionSliceCycles(4.0);
    CHECK_TRUE(sliceDecision.executionSliceComplete);
    CHECK_TRUE(sliceDecision.frontendServiceDue);
    CHECK_TRUE(sliceEngine.stats().executionSlicesCompleted == 1u);
    CHECK_TRUE(sliceEngine.stats().frontendServiceChecks == 1u);
    CHECK_TRUE(sliceEngine.stats().lastExecutionSliceCycles >= 1000.0);

    sliceEngine.beginExecutionSlice();
    sliceDecision = sliceEngine.recordExecutionSliceCycles(4.0);
    CHECK_TRUE(!sliceDecision.executionSliceComplete);
    CHECK_TRUE(!sliceDecision.frontendServiceDue);
    CHECK_TRUE(sliceEngine.stats().executionSlicesEntered == 2u);

    return 0;
}

#undef CHECK_TRUE
