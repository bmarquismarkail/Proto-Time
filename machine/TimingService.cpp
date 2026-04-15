#include "machine/TimingService.hpp"

#include <algorithm>

namespace BMMQ {

void TimingService::configure(const TimingConfig& config)
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_ = config;
    stats_.throttled = config_.throttled;
    stats_.speedMultiplier = config_.speedMultiplier;
    stats_.effectiveClockHz = config_.baseClockHz * config_.speedMultiplier;
}

void TimingService::setThrottled(bool throttled) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_.throttled = throttled;
    stats_.throttled = throttled;
}

void TimingService::setPaused(bool paused) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    stats_.paused = paused;
}

void TimingService::setSpeedMultiplier(double multiplier) noexcept
{
    if (multiplier <= 0.0) {
        multiplier = 1.0;
    }
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_.speedMultiplier = multiplier;
    stats_.speedMultiplier = multiplier;
    stats_.effectiveClockHz = config_.baseClockHz * multiplier;
}

void TimingService::requestSingleStep() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    singleStepRequested_ = true;
}

void TimingService::start(std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    lastTick_ = now;
    stats_.cycleBudget = 0.0;
    stats_.cycleDebt = 0.0;
    stats_.effectiveClockHz = config_.baseClockHz * config_.speedMultiplier;
}

void TimingService::update(std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    stats_.effectiveClockHz = config_.baseClockHz * config_.speedMultiplier;

    // If paused, don't accumulate budget but refresh tick.
    if (stats_.paused) {
        lastTick_ = now;
        return;
    }

    const auto elapsedSec = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;

    // Accumulate only when throttled; still update budget for diagnostics in unthrottled mode.
    stats_.cycleBudget += elapsedSec * stats_.effectiveClockHz;

    const double maxBudget = std::max(config_.minInstructionCycles,
                                      stats_.effectiveClockHz * std::chrono::duration<double>(config_.maxCatchUp).count());
    if (stats_.cycleBudget > maxBudget) {
        stats_.cycleBudget = maxBudget;
        ++stats_.catchUpClampCount;
    }
}

bool TimingService::canExecute() const noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    if (stats_.paused) {
        return singleStepRequested_;
    }
    if (!config_.throttled) {
        return true;
    }
    return stats_.cycleBudget >= config_.minInstructionCycles;
}

void TimingService::charge(double retiredCycles) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const double charged = std::max(config_.minInstructionCycles, retiredCycles);
    stats_.cycleBudget = std::max(0.0, stats_.cycleBudget - charged);
    if (stats_.paused && singleStepRequested_) {
        singleStepRequested_ = false;
        ++stats_.singleStepsGranted;
    }
}

std::chrono::steady_clock::time_point TimingService::nextWakeTime(
    std::chrono::steady_clock::time_point now) const noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    if (!config_.throttled || stats_.paused || stats_.effectiveClockHz <= 0.0) {
        return now;
    }
    if (stats_.cycleBudget >= config_.minInstructionCycles) {
        return now;
    }
    const double cyclesUntil = std::max(0.0, config_.minInstructionCycles - stats_.cycleBudget);
    const double secondsUntil = cyclesUntil / stats_.effectiveClockHz;
    using namespace std::chrono;
    const auto dur = duration_cast<steady_clock::duration>(duration<double>(secondsUntil));
    return now + dur;
}

TimingStats TimingService::stats() const noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    return stats_;
}

} // namespace BMMQ
