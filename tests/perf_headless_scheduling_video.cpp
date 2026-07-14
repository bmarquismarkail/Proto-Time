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
#include "cores/gameboy/GameBoyInput.hpp"
#include "machine/InputService.hpp"
#include "machine/TimingService.hpp"
#include "machine/VideoService.hpp"
#include "machine/plugins/IoPlugin.hpp"
#include "machine/plugins/video/VideoPlugin.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr std::size_t kTargetFrames = 30u;
constexpr std::size_t kTargetInputTransitions = 6u;
constexpr double kGameBoyClockHz = 4'194'304.0;
constexpr double kFirstVBlankCycles = 65'664.0;
constexpr double kFrameCycles = 70'224.0;
constexpr std::int64_t kLatencyLimitNs = 16'000'000;
constexpr std::int64_t kInputInjectionDelayNs = 5'000'000;
constexpr std::uint32_t kLightestPixel = 0xFFE0F8D0u;
constexpr std::uint32_t kDarkestPixel = 0xFF081820u;

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
    std::int64_t presentStartedAtNs = 0;
    std::int64_t presentCompletedAtNs = 0;
    bool inputPressedVisible = false;
};

struct InputTransition {
    BMMQ::InputButtonMask mask = 0u;
    std::int64_t injectedAtNs = 0;
    std::int64_t visibleAtNs = 0;
    std::uint64_t visibleGeneration = 0u;
};

class TimingHeadlessInput final : public BMMQ::IDigitalInputSourcePlugin {
public:
    [[nodiscard]] BMMQ::InputPluginCapabilities capabilities() const noexcept override
    {
        return {
            .pollingSafe = true,
            .eventPumpSafe = true,
            .deterministic = true,
            .supportsDigital = true,
            .supportsAnalog = false,
            .fixedLogicalLayout = true,
            .hotSwapSafe = false,
            .liveSeek = false,
            .nonRealtimeOnly = false,
            .headlessSafe = true,
        };
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "headless-timing-input";
    }

    [[nodiscard]] bool open() override
    {
        return true;
    }

    void close() noexcept override {}

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override
    {
        return mask_.load(std::memory_order_acquire);
    }

    void inject(BMMQ::InputButtonMask mask) noexcept
    {
        mask_.store(mask, std::memory_order_release);
        revision_.fetch_add(1u, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t revision() const noexcept
    {
        return revision_.load(std::memory_order_acquire);
    }

private:
    std::atomic<BMMQ::InputButtonMask> mask_{0u};
    std::atomic<std::uint64_t> revision_{1u};
};

[[nodiscard]] std::vector<std::uint8_t> inputResponseRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    // Select the directional JOYP lines, then continuously map Right to the
    // background palette. With the zero-filled tile data this makes every
    // subsequently rendered pixel switch between the lightest and darkest
    // DMG shades through guest CPU/JOYP/PPU execution.
    constexpr std::uint8_t program[] = {
        0x3Eu, 0x20u,       // LD A,$20
        0xE0u, 0x00u,       // LDH ($FF00),A
        0xF0u, 0x00u,       // loop: LDH A,($FF00)
        0xE6u, 0x01u,       // AND $01 (Right is active-low)
        0x20u, 0x04u,       // JR NZ,unpressed
        0x3Eu, 0xFFu,       // LD A,$FF (color 0 -> darkest)
        0x18u, 0x02u,       // JR write_palette
        0x3Eu, 0xFCu,       // unpressed: LD A,$FC (color 0 -> lightest)
        0xE0u, 0x47u,       // write_palette: LDH ($FF47),A
        0x18u, 0xF0u,       // JR loop
    };
    std::copy(std::begin(program), std::end(program), rom.begin() + 0x0100u);
    return rom;
}

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
        const auto started = clockNs(Clock::now());
        if (frame.pixels.empty()) {
            return false;
        }
        const auto lastPixel = frame.pixels.back();
        if (lastPixel != kLightestPixel && lastPixel != kDarkestPixel) {
            return false;
        }
        frames_.push_back({frame.generation,
                           started,
                           clockNs(Clock::now()),
                           lastPixel == kDarkestPixel});
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
        packet->packet.generation = generation;
        packet->packet.eventType = event.type;
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
    machine.loadRom(inputResponseRom());
    auto input = std::make_unique<TimingHeadlessInput>();
    auto* inputView = input.get();
    if (!machine.inputService().attachAdapter(std::move(input)) ||
        !machine.inputService().resume()) {
        std::cerr << "failed to initialize headless input adapter\n";
        return 1;
    }
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
    std::atomic<std::size_t> schedulerExecutedInstructions{0u};
    std::atomic<std::size_t> schedulerCompletedSlices{0u};
    std::atomic<std::size_t> schedulerIdleWaits{0u};
    std::vector<InputTransition> inputTransitions;
    inputTransitions.reserve(kTargetInputTransitions);

    std::thread renderLane([&]() {
        std::int64_t nextInputAtNs = 0;
        bool nextInputPressed = true;
        while (!emulationDone.load(std::memory_order_acquire) || video.hasPendingRealtimeFrame()) {
            const auto nowNs = clockNs(Clock::now());
            if (nextInputAtNs != 0 && nowNs >= nextInputAtNs) {
                const auto mask = nextInputPressed
                    ? BMMQ::inputButtonMask(BMMQ::InputButton::Right)
                    : BMMQ::InputButtonMask{0u};
                inputView->inject(mask);
                inputTransitions.push_back({mask, nowNs, 0, 0u});
                nextInputPressed = !nextInputPressed;
                nextInputAtNs = 0;
            }
            if (video.hasPendingRealtimeFrame()) {
                if (!video.presentOneFrame()) {
                    timedOut.store(true, std::memory_order_release);
                    break;
                }
                const auto& presented = presenterView->frames().back();
                if (!inputTransitions.empty()) {
                    auto& pending = inputTransitions.back();
                    const bool expectedPressed = pending.mask != 0u;
                    if (pending.visibleAtNs == 0 &&
                        presented.presentCompletedAtNs >= pending.injectedAtNs &&
                        presented.inputPressedVisible == expectedPressed) {
                        pending.visibleAtNs = presented.presentCompletedAtNs;
                        pending.visibleGeneration = presented.generation;
                    }
                }
                if (inputTransitions.size() < kTargetInputTransitions &&
                    (inputTransitions.empty() || inputTransitions.back().visibleAtNs != 0) &&
                    presented.generation >= 3u + inputTransitions.size() * 4u) {
                    nextInputAtNs = presented.presentCompletedAtNs + kInputInjectionDelayNs;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread emulationLane([&]() {
        const auto deadline = startedAt + std::chrono::seconds(2);
        std::uint64_t servicedInputRevision = 0u;
        while (bridgeView->published() < kTargetFrames && !bridgeView->failed()) {
            const auto now = Clock::now();
            if (now >= deadline) {
                timedOut.store(true, std::memory_order_release);
                break;
            }
            timing.update(now);
            schedulerUpdates.fetch_add(1u, std::memory_order_relaxed);
            const auto inputRevision = inputView->revision();
            if (inputRevision != servicedInputRevision) {
                machine.serviceInput();
                servicedInputRevision = inputRevision;
            }
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
                schedulerExecutedInstructions.fetch_add(1u, std::memory_order_relaxed);
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
    std::vector<std::int64_t> presentationDurations;
    std::vector<std::int64_t> inputResponseLatencies;
    transportLatencies.reserve(presentations.size());
    schedulingLatencies.reserve(presentations.size());
    presentationDurations.reserve(presentations.size());
    inputResponseLatencies.reserve(inputTransitions.size());
    for (const auto& presented : presentations) {
        if (presented.generation == 0u || presented.generation > publications.size()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(presented.generation - 1u);
        transportLatencies.push_back(std::max<std::int64_t>(
            0, presented.presentStartedAtNs - publications[index]));
        presentationDurations.push_back(std::max<std::int64_t>(
            0, presented.presentCompletedAtNs - presented.presentStartedAtNs));
        const auto idealCycles = kFirstVBlankCycles +
            static_cast<double>(index) * kFrameCycles;
        const auto idealNs = clockNs(startedAt) + static_cast<std::int64_t>(
            (idealCycles / kGameBoyClockHz) * 1'000'000'000.0);
        schedulingLatencies.push_back(std::max<std::int64_t>(
            0, presented.presentCompletedAtNs - idealNs));
    }
    for (const auto& transition : inputTransitions) {
        if (transition.visibleAtNs != 0) {
            inputResponseLatencies.push_back(std::max<std::int64_t>(
                0, transition.visibleAtNs - transition.injectedAtNs));
        }
    }

    const auto transportP99 = percentile(transportLatencies, 0.99);
    const auto schedulingP99 = percentile(schedulingLatencies, 0.99);
    const auto presentationP99 = percentile(presentationDurations, 0.99);
    const auto inputResponseP99 = percentile(inputResponseLatencies, 0.99);
    const auto diagnostics = video.diagnostics();
    const auto timingStats = timing.stats();
    const bool structuralPass = !timedOut.load(std::memory_order_acquire) &&
        !bridgeView->failed() &&
        bridgeView->published() == kTargetFrames &&
        presentations.size() == kTargetFrames &&
        transportLatencies.size() == kTargetFrames &&
        presentationDurations.size() == kTargetFrames &&
        inputTransitions.size() == kTargetInputTransitions &&
        inputResponseLatencies.size() == kTargetInputTransitions &&
        schedulerUpdates.load(std::memory_order_relaxed) > 0u &&
        schedulerExecutedInstructions.load(std::memory_order_relaxed) > 0u &&
        timingStats.executionSlicesEntered > 0u &&
        schedulerIdleWaits.load(std::memory_order_relaxed) > 0u &&
        diagnostics.overwriteRealtimeFrameCount == 0u &&
        diagnostics.staleEpochDropCount == 0u;
    const bool latencyPass = !kEnforceWallClockThresholds ||
        (transportP99 < kLatencyLimitNs &&
         presentationP99 < kLatencyLimitNs &&
         schedulingP99 < kLatencyLimitNs &&
         inputResponseP99 < kLatencyLimitNs);

    std::cout << "headless scheduling frames_published=" << bridgeView->published()
              << " frames_presented=" << presentations.size()
              << " overwrites=" << diagnostics.overwriteRealtimeFrameCount
              << " stale_epoch_drops=" << diagnostics.staleEpochDropCount
              << " scheduler_updates=" << schedulerUpdates.load(std::memory_order_relaxed)
              << " executed_instructions=" << schedulerExecutedInstructions.load(std::memory_order_relaxed)
              << " execution_slices_entered=" << timingStats.executionSlicesEntered
              << " execution_slices=" << schedulerCompletedSlices.load(std::memory_order_relaxed)
              << " idle_waits=" << schedulerIdleWaits.load(std::memory_order_relaxed)
              << " input_transitions=" << inputTransitions.size()
              << " input_responses=" << inputResponseLatencies.size() << '\n';
    std::cout << "gate headless_frame_residence_p99 observed_ns=" << transportP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    std::cout << "gate headless_presentation_p99 observed_ns=" << presentationP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    std::cout << "gate headless_scheduling_to_present_p99 observed_ns=" << schedulingP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    std::cout << "gate headless_input_to_frame_response_p99 observed_ns=" << inputResponseP99
              << " limit_ns=" << kLatencyLimitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << ((structuralPass && latencyPass) ? "pass" : "FAIL") << '\n';
    return structuralPass && latencyPass ? 0 : 1;
}
