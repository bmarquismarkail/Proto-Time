#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/TimingService.hpp"
#include "machine/VideoService.hpp"
#include "machine/plugins/IoPlugin.hpp"
#include "machine/plugins/video/VideoPlugin.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr std::size_t kTargetFrames = 30u;
constexpr double kGameBoyClockHz = 4'194'304.0;
constexpr double kFirstVBlankCycles = 65'664.0;
constexpr double kFrameCycles = 70'224.0;
constexpr std::int64_t kLatencyLimitNs = 16'000'000;

#if defined(BMMQ_TSAN_ENABLED) || defined(__SANITIZE_THREAD__)
constexpr bool kEnforceWallClockThresholds = false;
#else
constexpr bool kEnforceWallClockThresholds = true;
#endif

[[nodiscard]] std::int64_t clockNs(Clock::time_point value) noexcept
{
    return std::chrono::duration_cast<Nanoseconds>(value.time_since_epoch()).count();
}

[[nodiscard]] std::int64_t percentile(std::vector<std::int64_t> values, double quantile)
{
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(
        std::max(1.0, std::ceil(quantile * static_cast<double>(values.size()))));
    return values[std::min(rank - 1u, values.size() - 1u)];
}

struct PresentedFrame {
    std::uint64_t generation = 0u;
    std::int64_t presentedAtNs = 0;
};

class TimingHeadlessPresenter final : public BMMQ::IVideoPresenterPlugin {
public:
    TimingHeadlessPresenter()
    {
        frames_.reserve(kTargetFrames);
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "headless-timing";
    }

    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = false,
            .frameSizePreserving = true,
            .snapshotAware = true,
            .deterministic = true,
            .headlessSafe = true,
        };
    }

    bool open(const BMMQ::VideoPresenterConfig&) override
    {
        ready_ = true;
        return true;
    }

    void close() noexcept override
    {
        ready_ = false;
    }

    [[nodiscard]] bool ready() const noexcept override
    {
        return ready_;
    }

    bool present(const BMMQ::VideoFramePacket& frame) noexcept override
    {
        if (frames_.size() >= frames_.capacity()) {
            return false;
        }
        frames_.push_back({frame.generation, clockNs(Clock::now())});
        return true;
    }

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return {};
    }

    [[nodiscard]] const std::vector<PresentedFrame>& frames() const noexcept
    {
        return frames_;
    }

private:
    bool ready_ = false;
    std::vector<PresentedFrame> frames_{};
};

class SchedulingVideoBridge final : public BMMQ::IVideoPlugin {
public:
    explicit SchedulingVideoBridge(BMMQ::VideoService& videoService)
        : videoService_(videoService)
    {
        publishedAtNs_.reserve(kTargetFrames);
    }

    [[nodiscard]] std::string_view id() const override
    {
        return "test.headless-scheduling-video";
    }

    void onVideoEvent(const BMMQ::MachineEvent& event,
                      const BMMQ::MachineView& view) override
    {
        if (event.type != BMMQ::MachineEventType::VBlank ||
            published_.load(std::memory_order_relaxed) >= kTargetFrames) {
            return;
        }
        auto packet = view.realtimeVideoPacket({160, 144});
        if (!packet.has_value()) {
            failed_.store(true, std::memory_order_release);
            return;
        }
        const auto generation = published_.load(std::memory_order_relaxed) + 1u;
        packet->generation = generation;
        packet->eventType = event.type;
        const auto started = clockNs(Clock::now());
        if (!videoService_.publishRealtimeVideoPacket(std::move(*packet))) {
            failed_.store(true, std::memory_order_release);
            return;
        }
        publishedAtNs_.push_back(started);
        published_.store(generation, std::memory_order_release);
    }

    [[nodiscard]] std::size_t published() const noexcept
    {
        return published_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return failed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] const std::vector<std::int64_t>& publishedAtNs() const noexcept
    {
        return publishedAtNs_;
    }

private:
    BMMQ::VideoService& videoService_;
    std::atomic<std::size_t> published_{0u};
    std::atomic<bool> failed_{false};
    std::vector<std::int64_t> publishedAtNs_{};
};

} // namespace

int main()
{
    BMMQ::VideoService video({.frameWidth = 160, .frameHeight = 144, .mailboxDepthFrames = 1u});
    auto presenter = std::make_unique<TimingHeadlessPresenter>();
    auto* presenterView = presenter.get();
    if (!video.attachPresenter(std::move(presenter)) || !video.resume()) {
        std::cerr << "failed to initialize headless presenter\n";
        return 1;
    }

    GB::GameBoyMachine machine;
    machine.loadRom(std::vector<std::uint8_t>(0x8000u, 0x00u));
    auto bridge = std::make_unique<SchedulingVideoBridge>(video);
    auto* bridgeView = bridge.get();
    machine.pluginManager().add(std::move(bridge));
    machine.pluginManager().initialize(machine.mutableView());

    BMMQ::TimingConfig timingConfig;
    timingConfig.baseClockHz = kGameBoyClockHz;
    timingConfig.minInstructionCycles = 4.0;
    timingConfig.executionSliceSeconds = 0.0005;
    timingConfig.frontendServiceSliceSeconds = 0.0005;
    timingConfig.maxCatchUp = std::chrono::milliseconds(4);
    timingConfig.minSleepQuantum = std::chrono::microseconds(50);
    timingConfig.maxExecutionSlicesPerWake = 4u;
    timingConfig.maxCyclesPerWake = 16'384.0;
    timingConfig.throttled = true;

    const auto startedAt = Clock::now();
    BMMQ::TimingEngine timing(timingConfig);
    timing.start(startedAt);
    std::atomic<bool> emulationDone{false};
    std::atomic<bool> timedOut{false};
    std::atomic<std::size_t> schedulerUpdates{0u};
    std::atomic<std::size_t> schedulerCompletedSlices{0u};
    std::atomic<std::size_t> schedulerIdleWaits{0u};

    std::thread renderLane([&]() {
        while (!emulationDone.load(std::memory_order_acquire) || video.hasPendingRealtimeFrame()) {
            if (video.hasPendingRealtimeFrame()) {
                if (!video.presentOneFrame()) {
                    timedOut.store(true, std::memory_order_release);
                    break;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread emulationLane([&]() {
        const auto deadline = startedAt + std::chrono::seconds(2);
        while (bridgeView->published() < kTargetFrames && !bridgeView->failed()) {
            const auto now = Clock::now();
            if (now >= deadline) {
                timedOut.store(true, std::memory_order_release);
                break;
            }
            timing.update(now);
            schedulerUpdates.fetch_add(1u, std::memory_order_relaxed);
            bool executed = false;
            std::uint32_t slices = 0u;
            double wakeCycles = 0.0;
            bool sliceActive = false;
            while (timing.canExecute() &&
                   slices < timingConfig.maxExecutionSlicesPerWake &&
                   wakeCycles < timingConfig.maxCyclesPerWake) {
                if (!sliceActive) {
                    timing.beginExecutionSlice();
                    sliceActive = true;
                    ++slices;
                }
                machine.step();
                const auto retired = static_cast<double>(
                    machine.runtimeContext().getLastFeedback().retiredCycles);
                const auto charged = std::max(timingConfig.minInstructionCycles, retired);
                wakeCycles += charged;
                timing.charge(retired);
                executed = true;
                if (timing.recordExecutionSliceCycles(charged).executionSliceComplete) {
                    schedulerCompletedSlices.fetch_add(1u, std::memory_order_relaxed);
                    sliceActive = false;
                }
            }
            if (!executed) {
                schedulerIdleWaits.fetch_add(1u, std::memory_order_relaxed);
                const auto idleNow = Clock::now();
                const auto wake = timing.nextWakeTime(idleNow);
                if (timing.shouldSleep(idleNow) && wake > idleNow) {
                    std::this_thread::sleep_until(wake);
                } else {
                    std::this_thread::yield();
                }
            }
        }
        emulationDone.store(true, std::memory_order_release);
    });

    emulationLane.join();
    renderLane.join();
    machine.pluginManager().shutdown(machine.mutableView());

    const auto& publications = bridgeView->publishedAtNs();
    const auto& presentations = presenterView->frames();
    std::vector<std::int64_t> transportLatencies;
    std::vector<std::int64_t> schedulingLatencies;
    transportLatencies.reserve(presentations.size());
    schedulingLatencies.reserve(presentations.size());
    for (const auto& presented : presentations) {
        if (presented.generation == 0u || presented.generation > publications.size()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(presented.generation - 1u);
        transportLatencies.push_back(std::max<std::int64_t>(
            0, presented.presentedAtNs - publications[index]));
        const auto idealCycles = kFirstVBlankCycles +
            static_cast<double>(index) * kFrameCycles;
        const auto idealNs = clockNs(startedAt) + static_cast<std::int64_t>(
            (idealCycles / kGameBoyClockHz) * 1'000'000'000.0);
        schedulingLatencies.push_back(std::max<std::int64_t>(
            0, presented.presentedAtNs - idealNs));
    }

    const auto transportP99 = percentile(transportLatencies, 0.99);
    const auto schedulingP99 = percentile(schedulingLatencies, 0.99);
    const auto diagnostics = video.diagnostics();
    const bool structuralPass = !timedOut.load(std::memory_order_acquire) &&
        !bridgeView->failed() &&
        bridgeView->published() == kTargetFrames &&
        presentations.size() == kTargetFrames &&
        transportLatencies.size() == kTargetFrames &&
        schedulerUpdates.load(std::memory_order_relaxed) > 0u &&
        schedulerCompletedSlices.load(std::memory_order_relaxed) > 0u &&
        schedulerIdleWaits.load(std::memory_order_relaxed) > 0u &&
        diagnostics.overwriteRealtimeFrameCount == 0u &&
        diagnostics.staleEpochDropCount == 0u;
    const bool latencyPass = !kEnforceWallClockThresholds ||
        (transportP99 < kLatencyLimitNs && schedulingP99 < kLatencyLimitNs);

    std::cout << "headless scheduling frames_published=" << bridgeView->published()
              << " frames_presented=" << presentations.size()
              << " overwrites=" << diagnostics.overwriteRealtimeFrameCount
              << " stale_epoch_drops=" << diagnostics.staleEpochDropCount
              << " scheduler_updates=" << schedulerUpdates.load(std::memory_order_relaxed)
              << " execution_slices=" << schedulerCompletedSlices.load(std::memory_order_relaxed)
              << " idle_waits=" << schedulerIdleWaits.load(std::memory_order_relaxed) << '\n';
    std::cout << "gate headless_transport_p99 observed_ns=" << transportP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    std::cout << "gate headless_scheduling_to_present_p99 observed_ns=" << schedulingP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    return structuralPass && latencyPass ? 0 : 1;
}
