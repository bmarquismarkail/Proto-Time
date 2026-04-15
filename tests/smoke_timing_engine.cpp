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
    cfg.throttled = true;

    BMMQ::TimingEngine engine(cfg);

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

    // Paused mode is applied as an outer-loop control snapshot, not by service calls
    // inside the instruction loop.
    BMMQ::TimingControlState paused;
    paused.paused = true;
    engine.applyControl(paused);
    engine.update(t0 + std::chrono::milliseconds(300));
    CHECK_TRUE(!engine.canExecute());
    paused.singleStepRequested = true;
    engine.applyControl(paused);
    CHECK_TRUE(engine.canExecute());
    engine.charge(4.0); // consume single step
    CHECK_TRUE(!engine.canExecute());
    CHECK_TRUE(engine.stats().singleStepsGranted == 1u);

    return 0;
}

#undef CHECK_TRUE
