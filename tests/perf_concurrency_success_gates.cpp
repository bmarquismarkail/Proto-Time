#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/AudioService.hpp"
#include "machine/InputService.hpp"
#include "machine/plugins/video/VideoEngine.hpp"
#include "PerfTimingSupport.hpp"

namespace {

using BMMQ::Tests::Perf::Clock;
using BMMQ::Tests::Perf::Nanoseconds;
using BMMQ::Tests::Perf::kEnforceWallClockThresholds;
using BMMQ::Tests::Perf::percentile;

constexpr std::int64_t kAudioCallbackP99LimitNs = 50'000;
constexpr std::int64_t kVideoFrameAgeP99LimitNs = 16'000'000;
constexpr std::int64_t kInputHandoffP99LimitNs = 16'000'000;
constexpr std::int64_t kVideoSnapshotP95LimitNs = 10'000;
constexpr std::int64_t kAudioSnapshotP95LimitNs = 5'000;

struct Metric {
    std::string_view name;
    std::int64_t observedNs = 0;
    std::int64_t limitNs = 0;
};

template <typename Operation>
[[nodiscard]] std::vector<std::int64_t> sampleDurations(std::size_t sampleCount, Operation operation)
{
    for (std::size_t i = 0; i < 64u; ++i) {
        operation(i);
    }
    std::vector<std::int64_t> samples;
    samples.reserve(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const auto started = Clock::now();
        operation(i + 64u);
        samples.push_back(std::chrono::duration_cast<Nanoseconds>(Clock::now() - started).count());
    }
    return samples;
}

[[nodiscard]] bool enforce(const Metric& metric)
{
    const bool passed = !kEnforceWallClockThresholds || metric.observedNs < metric.limitNs;
    std::cout << "gate " << metric.name
              << " observed_ns=" << metric.observedNs
              << " limit_ns=" << metric.limitNs
              << " enforced=" << (kEnforceWallClockThresholds ? "true" : "false")
              << " result=" << (passed ? "pass" : "FAIL") << '\n';
    return passed;
}

[[nodiscard]] BMMQ::VideoPresentPacket packedFrame(std::uint64_t generation,
                                                   std::uint32_t colorA,
                                                   std::uint32_t colorB)
{
    BMMQ::VideoPresentPacket packet;
    packet.width = 160;
    packet.height = 144;
    packet.generation = generation;
    packet.source = BMMQ::VideoFrameSource::RealtimeSnapshot;
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(packet.width * packet.height), 0u);
    for (std::size_t i = 1u; i < indices.size(); i += 2u) {
        indices[i] = 1u;
    }
    const std::array<std::uint32_t, 2u> palette{colorA, colorB};
    packet.surface = BMMQ::makeIndexedVideoSurface(
        indices, packet.width, packet.height,
        BMMQ::RealtimeVideoEncoding::Indexed2, palette);
    return packet;
}

} // namespace

int main()
{
    bool passed = true;
    std::uint64_t checksum = 0u;

    // Audio: exercise the production drain-only method with a prepared block for
    // every callback. Production work is deliberately outside the timed region.
    BMMQ::AudioService audio;
    if (!audio.configureOutputTransport({
            .deviceSampleRate = 48'000,
            .channelCount = 1u,
            .callbackChunkSamples = 256u,
            .readyQueueChunks = 1u,
        })) {
        std::cerr << "failed to configure audio transport\n";
        return 1;
    }
    std::vector<std::int16_t> source(256u, 1234);
    std::vector<std::int16_t> output(256u, 0);
    std::vector<std::int64_t> audioDurations;
    audioDurations.reserve(2'000u);
    bool audioPreparedEveryCallback = true;
    for (std::size_t iteration = 0; iteration < 2'064u; ++iteration) {
        audio.appendRecentPcm(source, iteration + 1u);
        if (!audio.produceReadyOutputBlock()) {
            audioPreparedEveryCallback = false;
            break;
        }
        const auto started = Clock::now();
        audio.drainReadyOutput(std::span<std::int16_t>(output.data(), output.size()));
        const auto elapsed = std::chrono::duration_cast<Nanoseconds>(Clock::now() - started);
        audio.noteDrainCallbackDuration(elapsed);
        if (iteration >= 64u) {
            audioDurations.push_back(elapsed.count());
        }
        checksum += static_cast<std::uint16_t>(output.front());
    }
    const auto audioP99 = percentile(audioDurations, 0.99);
    passed = enforce({"audio_callback_p99", audioP99, kAudioCallbackP99LimitNs}) && passed;
    const auto audioStats = audio.transportStats();
    const bool audioIntegrity = audioPreparedEveryCallback &&
                                audioDurations.size() == 2'000u &&
                                audioStats.underrunCount == 0u &&
                                audioStats.silenceSamplesFilled == 0u &&
                                audioStats.drainCallbackDurationP99Nanos <= kAudioCallbackP99LimitNs;
    std::cout << "gate audio_integrity underruns=" << audioStats.underrunCount
              << " silence_samples=" << audioStats.silenceSamplesFilled
              << " diagnostics_p99_bound_ns=" << audioStats.drainCallbackDurationP99Nanos
              << " result=" << (audioIntegrity ? "pass" : "FAIL") << '\n';
    passed = audioIntegrity && passed;

    // Video: include mailbox publication, latest-frame consumption, and packed
    // frame reconstruction in the age budget.
    BMMQ::VideoEngine video({.frameWidth = 160, .frameHeight = 144, .mailboxDepthFrames = 1u});
    const auto videoTemplate = packedFrame(1u, 0xFF081820u, 0xFFE0F8D0u);
    const auto videoDurations = sampleDurations(1'000u, [&](std::size_t iteration) {
        auto packet = videoTemplate;
        packet.generation = iteration + 1u;
        const auto result = video.submitPresentPacket(std::move(packet));
        if (!result.accepted) {
            return;
        }
        auto consumed = video.tryConsumeLatestFrame();
        if (!consumed.has_value()) {
            return;
        }
        auto frame = BMMQ::makeFramePacket(std::move(*consumed));
        if (!BMMQ::materializeVideoFrameArgb(frame)) {
            return;
        }
        checksum += frame.pixels.empty() ? 0u : frame.pixels.front();
    });
    passed = enforce({"video_frame_age_p99", percentile(videoDurations, 0.99),
                      kVideoFrameAgeP99LimitNs}) && passed;

    // Input: publish-to-committed-state handoff. Visual response remains core/game
    // dependent, but the host-to-machine transport must consume far less than one frame.
    BMMQ::InputService input;
    const auto inputDurations = sampleDurations(2'000u, [&](std::size_t iteration) {
        const auto mask = static_cast<BMMQ::InputButtonMask>(iteration & 0xFFu);
        input.publishDigitalSnapshot(mask, iteration + 1u);
        const auto committed = input.committedDigitalMask();
        checksum += committed.value_or(0u);
    });
    passed = enforce({"input_handoff_p99", percentile(inputDurations, 0.99),
                      kInputHandoffP99LimitNs}) && passed;

    // MachineView snapshot construction limits from the plan. Use a warmed p95
    // to reject sustained regressions without treating a scheduler preemption as
    // snapshot work.
    GB::GameBoyMachine machine;
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    machine.loadRom(rom);
    machine.runtimeContext().write8(0x8000u, 0xA5u);
    machine.runtimeContext().write8(0xFE00u, 0x5Au);
    machine.runtimeContext().write8(0xFF43u, 0x17u);
    const auto view = machine.view();
    const auto videoSnapshotDurations = sampleDurations(1'000u, [&](std::size_t) {
        const auto state = view.videoState();
        checksum += state.has_value() ? state->vram.size() + state->oam.size() : 0u;
    });
    const auto audioSnapshotDurations = sampleDurations(1'000u, [&](std::size_t) {
        const auto state = view.audioState();
        checksum += state.has_value() ? state->registers.size() + state->waveRam.size() : 0u;
    });
    passed = enforce({"machine_view_video_snapshot_p95", percentile(videoSnapshotDurations, 0.95),
                      kVideoSnapshotP95LimitNs}) && passed;
    passed = enforce({"machine_view_audio_snapshot_p95", percentile(audioSnapshotDurations, 0.95),
                      kAudioSnapshotP95LimitNs}) && passed;
    const auto verifiedSnapshot = view.videoState();
    const bool snapshotCorrect = verifiedSnapshot.has_value() &&
                                 verifiedSnapshot->vram.size() == 0x2000u &&
                                 verifiedSnapshot->oam.size() == 0x00A0u &&
                                 verifiedSnapshot->vram.front() == 0xA5u &&
                                 verifiedSnapshot->oam.front() == 0x5Au &&
                                 verifiedSnapshot->scx == 0x17u;
    std::cout << "gate machine_view_video_snapshot_correct result="
              << (snapshotCorrect ? "pass" : "FAIL") << '\n';
    passed = snapshotCorrect && passed;

    // Determinism: identical guest input and execution must produce identical
    // transport bytes, not merely visually similar decoded frames.
    GB::GameBoyMachine twin;
    twin.loadRom(rom);
    twin.runtimeContext().write8(0x8000u, 0xA5u);
    twin.runtimeContext().write8(0xFE00u, 0x5Au);
    twin.runtimeContext().write8(0xFF43u, 0x17u);
    for (std::size_t i = 0; i < 20'000u; ++i) {
        machine.step();
        twin.step();
    }
    const auto frameA = machine.realtimeVideoPacket({160, 144});
    const auto frameB = twin.realtimeVideoPacket({160, 144});
    const bool deterministic = frameA.has_value() && frameB.has_value() &&
        frameA->packet.surface.encoding == frameB->packet.surface.encoding &&
        frameA->packet.surface.paletteArgb == frameB->packet.surface.paletteArgb &&
        frameA->packet.surface.indexedBytes == frameB->packet.surface.indexedBytes;
    std::cout << "gate deterministic_video_bytes result="
              << (deterministic ? "pass" : "FAIL") << '\n';
    passed = deterministic && passed;

    std::cout << "gate checksum=" << checksum << '\n';
    std::cout << (passed ? "concurrency-gates-pass" : "concurrency-gates-FAIL") << '\n';
    return passed ? 0 : 1;
}
