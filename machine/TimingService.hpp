#ifndef BMMQ_TIMING_SERVICE_HPP
#define BMMQ_TIMING_SERVICE_HPP

#include <chrono>
#include <cstdint>
#include <mutex>

namespace BMMQ {

struct TimingConfig {
    double baseClockHz = 0.0;
    double speedMultiplier = 1.0;
    double minInstructionCycles = 4.0;
    std::chrono::nanoseconds maxCatchUp = std::chrono::milliseconds(8);
    bool throttled = true;
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

    [[nodiscard]] std::chrono::steady_clock::time_point nextWakeTime(
        std::chrono::steady_clock::time_point now) const noexcept;

    [[nodiscard]] const TimingConfig& config() const noexcept { return config_; }
    [[nodiscard]] TimingStats stats() const noexcept;

private:
    mutable std::mutex nonRealTimeMutex_;
    TimingConfig config_{};
    TimingStats stats_{};
    std::chrono::steady_clock::time_point lastTick_{};
    bool singleStepRequested_ = false;
};

} // namespace BMMQ

#endif // BMMQ_TIMING_SERVICE_HPP
