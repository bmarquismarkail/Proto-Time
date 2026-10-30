#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "machine/AudioService.hpp"
#include "emulator/AudioKpiStatus.hpp"

int main()
{
    using Clock = std::chrono::steady_clock;
    BMMQ::AudioService audio;
    if (!audio.configureOutputTransport({48000, 1, 256, 3}) ||
        !audio.startOutputTransport()) {
        return 1;
    }

    std::atomic<bool> done{false};
    std::atomic<std::size_t> callbacks{0u};
    std::atomic<std::size_t> audibleCallbacks{0u};
    std::thread callback([&] {
        std::array<int16_t, 256> output{};
        while (!done.load(std::memory_order_acquire)) {
            audio.drainReadyOutput(output);
            if (std::any_of(output.begin(), output.end(), [](auto sample) { return sample != 0; })) {
                audibleCallbacks.fetch_add(1u, std::memory_order_relaxed);
            }
            callbacks.fetch_add(1u, std::memory_order_release);
            std::this_thread::yield();
        }
    });

    // The emulation/control lane is also the sole source producer. Only the
    // transport worker and callback run concurrently with its epoch changes.
    std::array<int16_t, 256> source{};
    source.fill(1234);
    std::uint64_t frame = 0u;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while ((audibleCallbacks.load(std::memory_order_relaxed) == 0u ||
            callbacks.load(std::memory_order_acquire) < 100u) && Clock::now() < deadline) {
        audio.appendRecentPcm(source, ++frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool startedDraining = audibleCallbacks.load(std::memory_order_relaxed) != 0u;

    // Before the fix, TSAN reports overlapping block.samples writes and drain
    // reads: clearReadyQueue resets both SPSC indices without quiescing either
    // owner. Exercise both public epoch-reset entrypoints while draining.
    for (unsigned iteration = 0; iteration < 6000u; ++iteration) {
        audio.appendRecentPcm(source, ++frame);
        if (iteration % 3u == 0u) {
            audio.bumpLifecycleEpochBarrier();
        } else if (iteration % 3u == 1u) {
            audio.setBackendPausedOrClosed(true);
        }
        std::this_thread::yield();
    }
    // Stopping production also clears the ring, and must tolerate an in-flight
    // drain. Physical backend callbacks still have to be joined before teardown.
    audio.stopOutputTransport();
    done.store(true, std::memory_order_release);
    callback.join();

    const auto stats = audio.transportStats();
    if (!startedDraining || callbacks.load() < 100u || stats.workerProducedBlocks == 0u ||
        stats.epochBumpCount < 4000u || stats.readyQueueDepth != 0u || stats.primedForDrain) {
        std::cerr << "concurrent audio lifecycle coverage or stop barrier failed\n";
        return 1;
    }

    // A stopped and reconfigured transport must remain usable and contain only
    // the newly supplied stream after reset.
    if (!audio.resetStream() || !audio.configureOutputTransport({48000, 1, 256, 1})) return 1;
    source.fill(4321);
    audio.appendRecentPcm(source, ++frame);
    if (!audio.produceReadyOutputBlock()) return 1;
    std::array<int16_t, 256> output{};
    audio.drainReadyOutput(output);
    if (!std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 4321; })) {
        std::cerr << "audio did not recover after concurrent epoch transitions\n";
        return 1;
    }

    // Distinguish an actual post-prime underrun from startup/transition silence,
    // then verify that new source data can prime the drained FIFO again.
    audio.drainReadyOutput(output);
    const auto starved = audio.transportStats();
    if (starved.underrunCount != 1u || starved.silenceSamplesFilled != output.size() ||
        starved.primedForDrain ||
        !std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 0; })) {
        std::cerr << "post-prime starvation was not zero-filled and counted\n";
        return 1;
    }
    audio.appendRecentPcm(source, ++frame);
    if (!audio.produceReadyOutputBlock() || !audio.primedForDrain()) return 1;
    audio.drainReadyOutput(output);
    if (audio.transportStats().underrunCount != 1u ||
        !std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 4321; })) {
        std::cerr << "audio did not re-prime after starvation\n";
        return 1;
    }
    // A real FIFO underrun can be hidden by an otherwise adequate average.
    BMMQ::AudioService kpiAudio;
    if (!kpiAudio.configureOutputTransport({48000, 1, 256, 1})) return 1;
    for (unsigned i = 0; i < 100u; ++i) {
        kpiAudio.appendRecentPcm(source, i + 1u);
        if (!kpiAudio.produceReadyOutputBlock()) return 1;
        kpiAudio.drainReadyOutput(output);
    }
    kpiAudio.drainReadyOutput(output);
    const auto kpiStats = kpiAudio.transportStats();
    const auto kpi = BMMQ::classifyAudioKpiStatus({
        .elapsedSeconds = 25600.0 / 48000.0,
        .generatedSamples = 25600u,
        .expectedSamplesPerSecond = 48000.0,
        .drainedReadySamples = kpiStats.drainReadySamples,
        .drainRequestedSamples = kpiStats.drainRequestedSamples,
        .underrunCount = kpiStats.underrunCount,
        .silenceSamplesFilled = kpiStats.silenceSamplesFilled,
    });
    if (kpiStats.underrunCount != 1u || kpi.drainRatio < 0.95 ||
        kpi.status != BMMQ::AudioKpiStatus::TransportLimited) {
        std::cerr << "actual FIFO underrun was hidden by a healthy average KPI\n";
        return 1;
    }
    std::cout << "audio lifecycle callbacks=" << callbacks.load()
              << " produced_blocks=" << stats.workerProducedBlocks
              << " epoch_bumps=" << stats.epochBumpCount << '\n';
    return 0;
}
