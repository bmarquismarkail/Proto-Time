#ifndef BMMQ_TIMING_SERVICE_HPP
#define BMMQ_TIMING_SERVICE_HPP

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace BMMQ {

enum class TimingPolicyProfile : std::uint8_t {
    Balanced = 0,
    LowLatency,
    PowerSaver,
    DeterministicTest,
};

struct TimingConfig {
    double baseClockHz = 0.0;
    double speedMultiplier = 1.0;
    double minInstructionCycles = 4.0;
    double executionSliceSeconds = 0.001;
    double frontendServiceSliceSeconds = 0.001;
    std::chrono::nanoseconds maxCatchUp = std::chrono::milliseconds(8);
    // Minimum host sleep quantum; deficits smaller than this should
    // not trigger a `sleep_until()` call in the emulator idle loop.
    std::chrono::nanoseconds minSleepQuantum = std::chrono::milliseconds(1);
    std::uint32_t maxExecutionSlicesPerWake = 4;
    double maxCyclesPerWake = 4096.0;
    bool adaptiveSleepEnabled = true;
    std::chrono::nanoseconds sleepSpinWindow = std::chrono::microseconds(200);
    std::chrono::nanoseconds sleepSpinCap = std::chrono::microseconds(250);
    bool throttled = true;
    TimingPolicyProfile profile = TimingPolicyProfile::Balanced;
};

struct TimingStats {
    double cycleBudget = 0.0;
    double cycleDebt = 0.0;
    double effectiveClockHz = 0.0;
    std::uint64_t wakeSleeps = 0;
    std::uint64_t catchUpClampCount = 0;
    std::uint64_t idleLoops = 0;
    std::uint64_t singleStepsGranted = 0;
    bool paused = false;
    bool throttled = true;
    double speedMultiplier = 1.0;
    // Host-sleep diagnostics
    std::uint64_t sleepDecisions = 0;
    std::uint64_t sleepSkippedForSmallDeficit = 0;
    std::chrono::nanoseconds configuredMinSleepQuantum = std::chrono::milliseconds(1);
    std::uint64_t executionSlicesEntered = 0;
    std::uint64_t executionSlicesCompleted = 0;
    std::uint64_t frontendServiceChecks = 0;
    double lastExecutionSliceCycles = 0.0;
    double currentExecutionSliceCycles = 0.0;
    std::uint64_t wakeBurstSamples = 0;
    std::uint64_t wakeBurstSliceLimitHitCount = 0;
    std::uint64_t wakeBurstCycleLimitHitCount = 0;
    std::uint64_t sleepCalls = 0;
    std::uint64_t sleepWakeEarlyCount = 0;
    std::uint64_t sleepWakeLateCount = 0;
    std::uint64_t sleepWakeJitterUnder100usCount = 0;
    std::uint64_t sleepWakeJitter100To500usCount = 0;
    std::uint64_t sleepWakeJitter500usTo2msCount = 0;
    std::uint64_t sleepWakeJitterOver2msCount = 0;
    std::uint64_t sleepWakeLateStreakCurrent = 0;
    std::uint64_t sleepWakeLateStreakHighWater = 0;
    std::uint64_t sleepOvershootCount = 0;
    std::chrono::nanoseconds sleepOvershootHighWater = std::chrono::nanoseconds::zero();
    std::chrono::nanoseconds sleepOvershootLast = std::chrono::nanoseconds::zero();
    std::uint32_t wakeBurstSlicesLast = 0;
    std::uint32_t wakeBurstSlicesHighWater = 0;
    double wakeBurstCyclesLast = 0.0;
    double wakeBurstCyclesHighWater = 0.0;
    std::uint64_t frontendTicksScheduled = 0;
    std::uint64_t frontendTicksExecuted = 0;
    std::uint64_t frontendTicksMerged = 0;
    std::chrono::nanoseconds frontendTickDelayLast = std::chrono::nanoseconds::zero();
    std::chrono::nanoseconds frontendTickDelayHighWater = std::chrono::nanoseconds::zero();
    TimingPolicyProfile activeProfile = TimingPolicyProfile::Balanced;
};

struct TimingControlState {
    bool paused = false;
    bool throttled = true;
    double speedMultiplier = 1.0;
    bool singleStepRequested = false;
};

struct TimingSliceDecision {
    bool executionSliceComplete = false;
    bool frontendServiceDue = false;
};

class TimingEngine {
public:
    TimingEngine() noexcept = default;
    explicit TimingEngine(const TimingConfig& config) noexcept;

    void configure(const TimingConfig& config) noexcept;
    [[nodiscard]] const TimingConfig& config() const noexcept { return config_; }

    void applyControl(const TimingControlState& control) noexcept;
    void start(std::chrono::steady_clock::time_point now) noexcept;
    void update(std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] bool canExecute() const noexcept;
    void charge(double retiredCycles) noexcept;
    void beginExecutionSlice() noexcept;
    [[nodiscard]] TimingSliceDecision recordExecutionSliceCycles(double chargedCycles) noexcept;

    [[nodiscard]] std::chrono::steady_clock::time_point nextWakeTime(
        std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] bool shouldSleep(std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] const TimingStats& stats() const noexcept { return stats_; }

private:
    TimingConfig config_{};
    TimingControlState control_{};
    TimingStats stats_{};
    std::chrono::steady_clock::time_point lastTick_{};
    double executionSliceCycles_ = 0.0;
    double frontendServiceSliceCycles_ = 0.0;
};

class TimingService {
public:
    TimingService() noexcept = default;

    void configure(const TimingConfig& config);

    void setThrottled(bool throttled) noexcept;
    void setPaused(bool paused) noexcept;
    void setSpeedMultiplier(double multiplier) noexcept;
    void requestSingleStep() noexcept;

    void start(std::chrono::steady_clock::time_point now) noexcept;
    void update(std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] bool canExecute() const noexcept;
    void charge(double retiredCycles) noexcept;
    void beginExecutionSlice() noexcept;
    [[nodiscard]] TimingSliceDecision recordExecutionSliceCycles(double chargedCycles) noexcept;

    [[nodiscard]] std::chrono::steady_clock::time_point nextWakeTime(
        std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] TimingControlState takeControlSnapshot() noexcept;
    void publishEngineStats(const TimingStats& stats) noexcept;

    [[nodiscard]] const TimingConfig& config() const noexcept { return config_; }
    [[nodiscard]] TimingStats stats() const noexcept;
    void recordWakeBurst(double burstCycles, std::uint32_t burstSlices) noexcept;
    void noteWakeBurstSliceLimitHit() noexcept;
    void noteWakeBurstCycleLimitHit() noexcept;
    void noteHostSleep(std::chrono::nanoseconds requested, std::chrono::nanoseconds actual) noexcept;
    void noteFrontendServiceTick(std::uint32_t scheduledTicks,
                                 std::uint32_t executedTicks,
                                 std::chrono::nanoseconds delay) noexcept;

private:
    // Protects access to the service-level configuration and the
    // internal engine state (`engine_`, `control_`, `stats_`). Public
    // mutators and reader methods (for example `configure()`,
    // `update()`, `charge()`, `canExecute()`, `nextWakeTime()`, and
    // `stats()`) acquire this mutex internally before accessing or
    // modifying these members. External callers MUST NOT attempt to
    // lock `nonRealTimeMutex_` directly — the class manages locking
    // internally. If external synchronization is required, prefer
    // using the public snapshot APIs such as `takeControlSnapshot()`
    // or `stats()` or add a dedicated public synchronization API.
    mutable std::mutex nonRealTimeMutex_;

    TimingConfig config_{};
    TimingEngine engine_{};
    TimingControlState control_{};
    TimingStats stats_{};
};

[[nodiscard]] const char* timingPolicyProfileName(TimingPolicyProfile profile) noexcept;
[[nodiscard]] TimingPolicyProfile parseTimingPolicyProfile(std::string_view value);
void applyTimingPolicyProfileDefaults(TimingPolicyProfile profile, TimingConfig& config) noexcept;

} // namespace BMMQ

#endif // BMMQ_TIMING_SERVICE_HPP
