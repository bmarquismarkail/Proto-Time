#pragma once

#include <chrono>
#include <thread>

#include "TimingService.hpp"

namespace BMMQ {

// Host-lane wait only: guest cycle accounting stays in TimingEngine. Share the
// production wait policy with the end-to-end gate so adaptive pacing is tested.
template <typename StopRequested>
void waitForTimingWake(std::chrono::steady_clock::time_point wake,
                       std::chrono::nanoseconds requestedSleep,
                       const TimingConfig& config, bool paused,
                       StopRequested stopRequested)
{
    using Clock = std::chrono::steady_clock;
    if (!paused && config.adaptiveSleepEnabled &&
        requestedSleep > config.sleepSpinWindow &&
        config.sleepSpinWindow > std::chrono::nanoseconds::zero()) {
        std::this_thread::sleep_until(wake - config.sleepSpinWindow);
        const auto spinStarted = Clock::now();
        while (!stopRequested() && Clock::now() < wake) {
            if (Clock::now() - spinStarted >= config.sleepSpinCap) break;
            std::this_thread::yield();
        }
    } else {
        std::this_thread::sleep_until(wake);
    }
}

} // namespace BMMQ
