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
    BMMQ::TimingService svc;
    BMMQ::TimingConfig cfg;
    cfg.baseClockHz = 1000000.0; // 1 MHz synthetic
    cfg.minInstructionCycles = 4.0;
    cfg.maxCatchUp = std::chrono::milliseconds(8);
    cfg.throttled = true;
    svc.configure(cfg);

    const auto t0 = SteadyClock::now();
    svc.start(t0);

    // Advance by 1ms -> should accumulate ~1000 cycles and be eligible to run
    svc.update(t0 + std::chrono::milliseconds(1));
    CHECK_TRUE(svc.canExecute());

    // Charge a small retired cycle count; service should charge at least minInstructionCycles
    svc.charge(1.0);
    // After charging minInstructionCycles, budget remains non-negative
    {
        const auto s = svc.stats();
        CHECK_TRUE(s.cycleBudget >= 0.0);
    }

    // Large update should clamp to maxCatchUp window
    svc.update(t0 + std::chrono::milliseconds(200));
    {
        const auto s = svc.stats();
        // With 1MHz and 8ms catch-up, max budget should be >= 8000 cycles
        CHECK_TRUE(s.cycleBudget <= std::max(4.0, cfg.baseClockHz * std::chrono::duration<double>(cfg.maxCatchUp).count()) + 1.0);
    }

    // Paused mode prevents execution unless single-step requested
    svc.setPaused(true);
    svc.update(SteadyClock::now() + std::chrono::milliseconds(1));
    CHECK_TRUE(!svc.canExecute());
    svc.requestSingleStep();
    CHECK_TRUE(svc.canExecute());
    svc.charge(4.0); // consume single step
    CHECK_TRUE(!svc.canExecute());

    return 0;
}

#undef CHECK_TRUE
