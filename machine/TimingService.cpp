#include "machine/TimingService.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace BMMQ {
namespace {
constexpr double kMinTimingSliceSeconds = 1e-6;

void updateCycleDebt(TimingStats& stats) noexcept
{
    stats.cycleDebt = std::max(0.0, -stats.cycleBudget);
}

// Engine snapshots must not erase service-owned host sleep, wake-burst, or
// UI tick measurements collected between publications.
void mergeEngineStats(TimingStats& destination, const TimingStats& source) noexcept
{
    destination.cycleBudget = source.cycleBudget;
    destination.cycleDebt = source.cycleDebt;
    destination.effectiveClockHz = source.effectiveClockHz;
    destination.catchUpClampCount = source.catchUpClampCount;
    destination.wakeSleeps = source.wakeSleeps;
    destination.idleLoops = source.idleLoops;
    destination.singleStepsGranted = source.singleStepsGranted;
    destination.paused = source.paused;
    destination.throttled = source.throttled;
    destination.speedMultiplier = source.speedMultiplier;
    destination.sleepDecisions = source.sleepDecisions;
    destination.sleepSkippedForSmallDeficit = source.sleepSkippedForSmallDeficit;
    destination.configuredMinSleepQuantum = source.configuredMinSleepQuantum;
    destination.executionSlicesEntered = source.executionSlicesEntered;
    destination.executionSlicesCompleted = source.executionSlicesCompleted;
    destination.frontendServiceChecks = source.frontendServiceChecks;
    destination.lastExecutionSliceCycles = source.lastExecutionSliceCycles;
    destination.currentExecutionSliceCycles = source.currentExecutionSliceCycles;
    destination.activeProfile = source.activeProfile;
    destination.configuredBatchInterval = source.configuredBatchInterval;
}

void sanitizeTimingConfig(TimingConfig& config) noexcept
{
    config.batchInterval = std::clamp(config.batchInterval,
        std::chrono::nanoseconds(std::chrono::microseconds(1)),
        std::chrono::nanoseconds(std::chrono::milliseconds(2)));
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
    if (config.sleepSpinWindow < std::chrono::nanoseconds::zero()) {
        config.sleepSpinWindow = std::chrono::nanoseconds::zero();
    }
    if (config.sleepSpinCap < std::chrono::nanoseconds::zero()) {
        config.sleepSpinCap = std::chrono::nanoseconds::zero();
    }
    if (config.sleepSpinWindow > config.sleepSpinCap) {
        config.sleepSpinWindow = config.sleepSpinCap;
    }
}
} // namespace

const char* timingPolicyProfileName(TimingPolicyProfile profile) noexcept
{
    switch (profile) {
    case TimingPolicyProfile::Balanced:
        return "balanced";
    case TimingPolicyProfile::LowLatency:
        return "low_latency";
    case TimingPolicyProfile::PowerSaver:
        return "power_saver";
    case TimingPolicyProfile::DeterministicTest:
        return "deterministic_test";
    }
    return "balanced";
}

TimingPolicyProfile parseTimingPolicyProfile(std::string_view value)
{
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower == "balanced") {
        return TimingPolicyProfile::Balanced;
    }
    if (lower == "low_latency" || lower == "low-latency") {
        return TimingPolicyProfile::LowLatency;
    }
    if (lower == "power_saver" || lower == "power-saver") {
        return TimingPolicyProfile::PowerSaver;
    }
    if (lower == "deterministic_test" || lower == "deterministic-test") {
        return TimingPolicyProfile::DeterministicTest;
    }
    throw std::invalid_argument("Unknown timing profile: " + std::string(value));
}

void applyTimingPolicyProfileDefaults(TimingPolicyProfile profile, TimingConfig& config) noexcept
{
    config.profile = profile;
    switch (profile) {
    case TimingPolicyProfile::Balanced:
        config.batchInterval = std::chrono::milliseconds(1);
        config.minSleepQuantum = std::chrono::milliseconds(1);
        config.maxExecutionSlicesPerWake = 4u;
        config.adaptiveSleepEnabled = true;
        config.sleepSpinWindow = std::chrono::microseconds(200);
        config.sleepSpinCap = std::chrono::microseconds(250);
        break;
    case TimingPolicyProfile::LowLatency:
        config.batchInterval = std::chrono::microseconds(250);
        config.minSleepQuantum = std::chrono::microseconds(250);
        config.maxExecutionSlicesPerWake = 3u;
        config.adaptiveSleepEnabled = true;
        config.sleepSpinWindow = std::chrono::microseconds(300);
        config.sleepSpinCap = std::chrono::microseconds(350);
        break;
    case TimingPolicyProfile::PowerSaver:
        config.batchInterval = std::chrono::milliseconds(2);
        config.minSleepQuantum = std::chrono::milliseconds(2);
        config.maxExecutionSlicesPerWake = 6u;
        config.adaptiveSleepEnabled = false;
        config.sleepSpinWindow = std::chrono::nanoseconds::zero();
        config.sleepSpinCap = std::chrono::nanoseconds::zero();
        break;
    case TimingPolicyProfile::DeterministicTest:
        config.batchInterval = std::chrono::microseconds(1);
        config.minSleepQuantum = std::chrono::microseconds(1);
        config.maxExecutionSlicesPerWake = 2u;
        config.adaptiveSleepEnabled = false;
        config.sleepSpinWindow = std::chrono::nanoseconds::zero();
        config.sleepSpinCap = std::chrono::nanoseconds::zero();
        break;
    }
}

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
    stats_.activeProfile = config_.profile;
    stats_.configuredBatchInterval = config_.batchInterval;
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
    stats_.activeProfile = config_.profile;
    stats_.configuredBatchInterval = config_.batchInterval;
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

std::chrono::steady_clock::time_point TimingEngine::nextBatchWakeTime(
    std::chrono::steady_clock::time_point now) noexcept
{
    if (control_.paused) {
        return control_.singleStepRequested ? now : now + config_.batchInterval;
    }
    if (!control_.throttled) return now;
    if (stats_.effectiveClockHz <= 0.0) return now + config_.batchInterval;
    // Unconsumed budget means bounded catch-up work is still pending.
    if (stats_.cycleBudget >= config_.minInstructionCycles) return now;

    const double interval = std::chrono::duration<double>(config_.batchInterval).count();
    const double catchUp = std::chrono::duration<double>(config_.maxCatchUp).count();
    const double targetCycles = std::max(config_.minInstructionCycles,
        stats_.effectiveClockHz * std::min(interval, std::max(0.0, catchUp)));
    const auto delay = std::chrono::ceil<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>((targetCycles - stats_.cycleBudget) / stats_.effectiveClockHz));
    // Anchor to the last budget update, not completion of housekeeping. This
    // subtracts execution time and repays instruction overshoot without drift.
    // Bound control/stop polling even for unusually slow guest clocks or debt.
    const auto wake = std::min(lastTick_ + delay, now + config_.batchInterval);
    if (wake > now) ++stats_.sleepDecisions;
    return std::max(now, wake);
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

std::chrono::steady_clock::time_point TimingService::nextBatchWakeTime(
    std::chrono::steady_clock::time_point now) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const auto wake = engine_.nextBatchWakeTime(now);
    mergeEngineStats(stats_, engine_.stats());
    stats_.configuredMinSleepQuantum = config_.minSleepQuantum;
    return wake;
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
    mergeEngineStats(stats_, stats);
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
    const auto delta = actual - requested;
    const auto absDelta = delta >= std::chrono::nanoseconds::zero() ? delta : -delta;

    if (absDelta < std::chrono::microseconds(100)) {
        ++stats_.sleepWakeJitterUnder100usCount;
    } else if (absDelta < std::chrono::microseconds(500)) {
        ++stats_.sleepWakeJitter100To500usCount;
    } else if (absDelta < std::chrono::milliseconds(2)) {
        ++stats_.sleepWakeJitter500usTo2msCount;
    } else {
        ++stats_.sleepWakeJitterOver2msCount;
    }

    if (delta < std::chrono::nanoseconds::zero()) {
        ++stats_.sleepWakeEarlyCount;
        stats_.sleepWakeLateStreakCurrent = 0;
        stats_.sleepOvershootLast = std::chrono::nanoseconds::zero();
        return;
    }

    ++stats_.sleepWakeLateCount;
    ++stats_.sleepWakeLateStreakCurrent;
    stats_.sleepWakeLateStreakHighWater =
        std::max(stats_.sleepWakeLateStreakHighWater, stats_.sleepWakeLateStreakCurrent);
    stats_.sleepOvershootLast = delta;
    if (delta > std::chrono::nanoseconds::zero()) {
        ++stats_.sleepOvershootCount;
        stats_.sleepOvershootHighWater = std::max(stats_.sleepOvershootHighWater, delta);
    }
}

void TimingService::noteFrontendServiceTick(std::uint32_t scheduledTicks,
                                            std::uint32_t executedTicks,
                                            std::chrono::nanoseconds delay) noexcept
{
    std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
    const auto sanitizedExecuted = std::min(executedTicks, scheduledTicks);
    stats_.frontendTicksScheduled += scheduledTicks;
    stats_.frontendTicksExecuted += sanitizedExecuted;
    stats_.frontendTicksMerged += (scheduledTicks - sanitizedExecuted);
    stats_.frontendTickDelayLast = std::max(delay, std::chrono::nanoseconds::zero());
    stats_.frontendTickDelayHighWater = std::max(stats_.frontendTickDelayHighWater, stats_.frontendTickDelayLast);
}

} // namespace BMMQ
