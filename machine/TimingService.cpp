#include "machine/TimingService.hpp"

#include <algorithm>

namespace BMMQ {
namespace {
constexpr double kMinTimingSliceSeconds = 1e-6;

void updateCycleDebt(TimingStats& stats) noexcept
{
    stats.cycleDebt = std::max(0.0, -stats.cycleBudget);
}

void sanitizeTimingConfig(TimingConfig& config) noexcept
{
    if (config.executionSliceSeconds <= 0.0) {
        config.executionSliceSeconds = kMinTimingSliceSeconds;
    }
    if (config.frontendServiceSliceSeconds <= 0.0) {
        config.frontendServiceSliceSeconds = config.executionSliceSeconds;
    }
    if (config.maxExecutionSlicesPerWake == 0u) {
        config.maxExecutionSlicesPerWake = 1u;
    }
    if (config.maxCyclesPerWake <= 0.0) {
        config.maxCyclesPerWake = config.minInstructionCycles;
    }
}
} // namespace

TimingEngine::TimingEngine(const TimingConfig& config) noexcept
{
    configure(config);
}

void TimingEngine::configure(const TimingConfig& config) noexcept
{
    config_ = config;
    sanitizeTimingConfig(config_);
    control_.throttled = config_.throttled;
    control_.speedMultiplier = config_.speedMultiplier;
    stats_.throttled = control_.throttled;
    stats_.speedMultiplier = control_.speedMultiplier;
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;
    updateCycleDebt(stats_);
}

void TimingEngine::applyControl(const TimingControlState& control) noexcept
{
    control_ = control;
    if (control_.speedMultiplier <= 0.0) {
        control_.speedMultiplier = 1.0;
    }
    config_.throttled = control_.throttled;
    config_.speedMultiplier = control_.speedMultiplier;
    stats_.paused = control_.paused;
    stats_.throttled = control_.throttled;
    stats_.speedMultiplier = control_.speedMultiplier;
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;
    updateCycleDebt(stats_);
}

void TimingEngine::start(std::chrono::steady_clock::time_point now) noexcept
{
    lastTick_ = now;
    stats_.cycleBudget = 0.0;
    stats_.cycleDebt = 0.0;
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;
    executionSliceCycles_ = 0.0;
    frontendServiceSliceCycles_ = 0.0;
    stats_.currentExecutionSliceCycles = 0.0;
}

void TimingEngine::update(std::chrono::steady_clock::time_point now) noexcept
{
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;

    if (control_.paused) {
        lastTick_ = now;
        return;
    }

    const auto elapsedSec = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    stats_.cycleBudget += elapsedSec * stats_.effectiveClockHz;

    const double maxBudget = std::max(config_.minInstructionCycles,
                                      stats_.effectiveClockHz * std::chrono::duration<double>(config_.maxCatchUp).count());
    if (stats_.cycleBudget > maxBudget) {
        stats_.cycleBudget = maxBudget;
        ++stats_.catchUpClampCount;
    }
    updateCycleDebt(stats_);
}

bool TimingEngine::canExecute() const noexcept
{
    if (control_.paused) {
        return control_.singleStepRequested;
    }
    if (!control_.throttled) {
        return true;
    }
    return stats_.cycleBudget >= config_.minInstructionCycles;
}

void TimingEngine::charge(double retiredCycles) noexcept
{
    const double charged = std::max(config_.minInstructionCycles, retiredCycles);
    if (!control_.throttled || control_.paused) {
        stats_.cycleBudget = std::max(0.0, stats_.cycleBudget - charged);
    } else {
        stats_.cycleBudget -= charged;
    }
    updateCycleDebt(stats_);
    if (control_.paused && control_.singleStepRequested) {
        control_.singleStepRequested = false;
        ++stats_.singleStepsGranted;
    }
}

void TimingEngine::beginExecutionSlice() noexcept
{
    executionSliceCycles_ = 0.0;
    stats_.currentExecutionSliceCycles = 0.0;
    ++stats_.executionSlicesEntered;
}

TimingSliceDecision TimingEngine::recordExecutionSliceCycles(double chargedCycles) noexcept
{
    const double charged = std::max(0.0, chargedCycles);
    executionSliceCycles_ += charged;
    frontendServiceSliceCycles_ += charged;
    stats_.currentExecutionSliceCycles = executionSliceCycles_;

    const double maxExecutionSliceCycles = std::max(
        config_.minInstructionCycles,
        config_.baseClockHz * config_.executionSliceSeconds);
    const double maxFrontendServiceSliceCycles = std::max(
        config_.minInstructionCycles,
        config_.baseClockHz * config_.frontendServiceSliceSeconds);

    TimingSliceDecision decision;
    if (frontendServiceSliceCycles_ >= maxFrontendServiceSliceCycles) {
        frontendServiceSliceCycles_ = 0.0;
        decision.frontendServiceDue = true;
        ++stats_.frontendServiceChecks;
    }
    if (executionSliceCycles_ >= maxExecutionSliceCycles) {
        decision.executionSliceComplete = true;
        ++stats_.executionSlicesCompleted;
        stats_.lastExecutionSliceCycles = executionSliceCycles_;
    }
    return decision;
}

std::chrono::steady_clock::time_point TimingEngine::nextWakeTime(
    std::chrono::steady_clock::time_point now) noexcept
{
    if (!control_.throttled || control_.paused || stats_.effectiveClockHz <= 0.0) {
        return now;
    }
    if (stats_.cycleBudget >= config_.minInstructionCycles) {
        return now;
    }
    const double cyclesUntil = std::max(0.0, config_.minInstructionCycles - stats_.cycleBudget);
    const double secondsUntil = cyclesUntil / stats_.effectiveClockHz;
    using namespace std::chrono;
    const auto dur = duration_cast<steady_clock::duration>(duration<double>(secondsUntil));

    // If the computed wait is smaller than the configured host sleep
    // quantum, return `now` to indicate we should not call
    // `sleep_until()` for tiny deficits.
    if (dur < config_.minSleepQuantum) {
        ++stats_.sleepSkippedForSmallDeficit;
        return now;
    }
    ++stats_.sleepDecisions;
    return now + dur;
}

bool TimingEngine::shouldSleep(std::chrono::steady_clock::time_point now) noexcept
{
    (void)now;
    if (!control_.throttled || control_.paused || stats_.effectiveClockHz <= 0.0) {
        return false;
    }
    if (stats_.cycleBudget >= config_.minInstructionCycles) {
        return false;
    }
    const double cyclesUntil = std::max(0.0, config_.minInstructionCycles - stats_.cycleBudget);
    const double secondsUntil = cyclesUntil / stats_.effectiveClockHz;
    using namespace std::chrono;
    const auto dur = duration_cast<steady_clock::duration>(duration<double>(secondsUntil));
    return dur >= config_.minSleepQuantum;
}

void TimingService::configure(const TimingConfig& config)
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_ = config;
    sanitizeTimingConfig(config_);
    control_.throttled = config_.throttled;
    control_.speedMultiplier = config_.speedMultiplier;
    control_.paused = false;
    control_.singleStepRequested = false;
    engine_.configure(config_);
    engine_.applyControl(control_);
    stats_ = engine_.stats();
    // Propagate configured min sleep quantum into service-level stats snapshot
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
}

void TimingService::setThrottled(bool throttled) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_.throttled = throttled;
    control_.throttled = throttled;
    engine_.applyControl(control_);
    stats_.throttled = throttled;
}

void TimingService::setPaused(bool paused) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    control_.paused = paused;
    engine_.applyControl(control_);
    stats_.paused = paused;
}

void TimingService::setSpeedMultiplier(double multiplier) noexcept
{
    if (multiplier <= 0.0) {
        multiplier = 1.0;
    }
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    config_.speedMultiplier = multiplier;
    control_.speedMultiplier = multiplier;
    engine_.applyControl(control_);
    stats_.speedMultiplier = multiplier;
    stats_.effectiveClockHz = config_.baseClockHz * multiplier;
}

void TimingService::requestSingleStep() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    control_.singleStepRequested = true;
    engine_.applyControl(control_);
}

void TimingService::start(std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    engine_.start(now);
    stats_ = engine_.stats();
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
}

void TimingService::update(std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    engine_.applyControl(control_);
    engine_.update(now);
    stats_ = engine_.stats();
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
}

bool TimingService::canExecute() const noexcept
{
    // Protect access to the engine's control and stats fields with the
    // nonRealTimeMutex_. Other mutators acquire this mutex before calling
    // into the engine (configure/update/charge/etc.), so match that
    // protection here to avoid data races.
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    return engine_.canExecute();
}

void TimingService::charge(double retiredCycles) noexcept
{
    // Acquire the same mutex used by other mutators to avoid races
    // against the engine's control/stats fields. Also detect whether
    // the engine consumed a single-step request (it increments
    // `singleStepsGranted` when it does) so we can clear the
    // service-level `control_.singleStepRequested` to avoid
    // re-enabling a consumed single-step via `update()`/applyControl().
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const auto prevSingleSteps = engine_.stats().singleStepsGranted;
    engine_.charge(retiredCycles);
    const auto newStats = engine_.stats();
    // If engine consumed a single-step, ensure service control reflects that.
    if (newStats.singleStepsGranted > prevSingleSteps) {
        control_.singleStepRequested = false;
    }
    stats_ = newStats;
}

void TimingService::beginExecutionSlice() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    engine_.beginExecutionSlice();
    stats_ = engine_.stats();
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
}

TimingSliceDecision TimingService::recordExecutionSliceCycles(double chargedCycles) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const auto decision = engine_.recordExecutionSliceCycles(chargedCycles);
    stats_ = engine_.stats();
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
    return decision;
}

std::chrono::steady_clock::time_point TimingService::nextWakeTime(
    std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    // Ensure service-level stats mirror engine diagnostics after the call
    const auto nt = engine_.nextWakeTime(now);
    stats_ = engine_.stats();
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
    return nt;
}

TimingControlState TimingService::takeControlSnapshot() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    auto snapshot = control_;
    control_.singleStepRequested = false;
    stats_.paused = control_.paused;
    stats_.throttled = control_.throttled;
    stats_.speedMultiplier = control_.speedMultiplier;
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;
    return snapshot;
}

void TimingService::publishEngineStats(const TimingStats& stats) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    stats_ = stats;
    stats_.paused = control_.paused;
    stats_.throttled = control_.throttled;
    stats_.speedMultiplier = control_.speedMultiplier;
    stats_.effectiveClockHz = config_.baseClockHz * control_.speedMultiplier;
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
}

TimingStats TimingService::stats() const noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    return stats_;
}

void TimingService::recordWakeBurst(double burstCycles, std::uint32_t burstSlices) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const auto sanitizedCycles = std::max(0.0, burstCycles);
    stats_.wakeBurstCyclesLast = sanitizedCycles;
    stats_.wakeBurstSlicesLast = burstSlices;
    stats_.wakeBurstCyclesHighWater = std::max(stats_.wakeBurstCyclesHighWater, sanitizedCycles);
    stats_.wakeBurstSlicesHighWater = std::max(stats_.wakeBurstSlicesHighWater, burstSlices);
    ++stats_.wakeBurstSamples;
}

void TimingService::noteWakeBurstSliceLimitHit() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    ++stats_.wakeBurstSliceLimitHitCount;
}

void TimingService::noteWakeBurstCycleLimitHit() noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    ++stats_.wakeBurstCycleLimitHitCount;
}

void TimingService::noteHostSleep(std::chrono::nanoseconds requested, std::chrono::nanoseconds actual) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    ++stats_.sleepCalls;
    if (actual <= requested) {
        stats_.sleepOvershootLast = std::chrono::nanoseconds::zero();
        return;
    }
    const auto overshoot = actual - requested;
    stats_.sleepOvershootLast = overshoot;
    ++stats_.sleepOvershootCount;
    stats_.sleepOvershootHighWater = std::max(stats_.sleepOvershootHighWater, overshoot);
}

} // namespace BMMQ
