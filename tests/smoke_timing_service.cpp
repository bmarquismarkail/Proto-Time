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
    cfg.throttled = true;
    svc.configure(cfg);

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

    return 0;
}

#undef CHECK_TRUE
