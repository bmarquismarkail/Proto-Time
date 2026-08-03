/////////////////////////////////////////////////////////////////////////
//
//	2020 Emulator Project Idea Mk 2
//	Author: Brandon M. M. Green
//
//	/////
//
// 	The purpose of this is to see the vision of this emulator system
//
//
/////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "emulator/AudioKpiStatus.hpp"
#include "emulator/EmulatorConfig.hpp"
#include "emulator/DiagnosticsJson.hpp"
#include "emulator/EmulatorHost.hpp"
#include "inst_cycle/executor/ExecutorPolicyRegistry.hpp"
#include "machine/BackgroundTaskService.hpp"
#include "machine/DebugSnapshotService.hpp"
#include "machine/ImageDecoder.hpp"
#include "machine/plugins/FrontendPluginLoader.hpp"
#include "machine/plugins/AudioOutputPluginLoader.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"
#include "machine/plugins/audio_output/DummyAudioOutput.hpp"
#include "machine/plugins/audio_output/FileAudioOutput.hpp"
#include "machine/plugins/audio/AudioTransportPlugin.hpp"
#include "machine/plugins/audio/PsgMidiPlugin.hpp"
#if defined(BMMQ_HAS_ALSA_MIDI)
#include "machine/plugins/audio/AlsaMidiSink.hpp"
#endif
#include "machine/TimingService.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;
#include "cores/gamegear/GameGearMachine.hpp"

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleSignal(int)
{
    gStopRequested = 1;
}

void printUsage(std::string_view program)
{
    std::cerr << "Usage: " << program << " --core <gameboy|gamegear> --rom <path> [options]\n"
              << "   or: " << program << " --core <gameboy|gamegear> <path> [options]\n\n"
              << "Options:\n"
              << "  --core <name>      Machine core to run: gameboy or gamegear\n"
              << "  --config <path>    Optional INI-style emulator configuration file\n"
              << "  --rom <path>       Cartridge ROM to load\n"
              << "  --boot-rom <path>  Optional external boot ROM for supported cores\n"
              << "  --frontend-plugin <path>\n"
              << "                     Load a frontend from a pure-C ABI module (--plugin is an alias)\n"
              << "  --frontend <id>    Select a module frontend ID, or headless\n"
              << "  --executor-plugin <path>\n"
              << "                     Load executor policies from a pure-C ABI module\n"
              << "  --executor-policy <id>\n"
              << "                     Select a built-in or module executor policy ID\n"
              << "  --ir-adapter-plugin <path> --ir-adapter-id <id>\n"
              << "                     Select a dynamic IR core adapter explicitly\n"
              << "  --ir-backend-plugin <path> --ir-backend-id <id>\n"
              << "                     Select a dynamic IR execution backend explicitly\n"
              << "  --steps <count>    Stop after a fixed number of instruction steps\n"
              << "  --scale <n>        Frontend window scale factor (default: 3)\n"
              << "  --hd-scale <n>     HD texture replacement scale (1-8, default: 1)\n"
              << "  --cpu-mode <mode>  CPU mode: baseline, block, ir, or native (experimental x86-64/POSIX)\n"
              << "  --cpu-detailed-timing\n"
              << "                     Enable intrusive IR guard/lowering/execution timers\n"
              << "  --unthrottled      Run unthrottled (no wall-clock pacing)\n"
              << "  --speed <mult>     Start with speed multiplier (e.g. 2.0)\n"
              << "  --pause            Start paused (use single-step to advance)\n"
              << "  --diagnostics-report <path>\n"
              << "                     Write periodic runtime diagnostics samples to <path>\n"
              << "  --diagnostics-interval-ms <n>\n"
              << "                     Diagnostics sample interval in milliseconds (default: 1000)\n"
              << "  --no-audio         Disable frontend audio output\n"
              << "  --audio-backend <name>\n"
              << "                     Audio backend: sdl, dummy, or file (default: sdl)\n"
              << "  --audio-plugin <path>\n"
              << "                     Load an audio output from a pure-C ABI module\n"
              << "  --audio-processor-plugin <path> (repeatable)\n"
              << "                     Load audio processors from a pure-C ABI module\n"
              << "  --audio-processor-config <id>=<json-file>\n"
              << "                     Configure an audio processor from a JSON file\n"
              << "  --audio-file <path>\n"
              << "                     Raw signed 16-bit PCM path for the file backend\n"
              << "  --midi-file <path>\n"
              << "                     Translate PSG events to a Standard MIDI File\n"
              << "  --midi-output <client:port|subscribers>\n"
              << "                     Send PSG events to an ALSA MIDI interface\n"
              << "  --audio-ready-queue-chunks <n>\n"
              << "  --audio-batch-chunks <n>\n"
              << "                     Audio output ready-queue chunk depth (1-64, default: 3)\n"
              << "  --background-workers <n>\n"
              << "                     Background worker count; 0 selects the reserved-core default\n"
              << "  --background-queue-capacity <n>\n"
              << "                     Maximum queued background jobs (default: 1024)\n"
              << "  --debug-snapshots  Enable optional background debug snapshots\n"
              << "  --visual-pack <path>\n"
              << "                     Load a visual override pack.json; repeat to load multiple packs\n"
              << "  --visual-capture <dir>\n"
              << "                     Capture observed decoded visual resources for pack authoring\n"
              << "  --visual-pack-reload\n"
              << "                     Poll visual pack manifests/assets and reload changed packs\n"
              << "  --headless         Run without a frontend plugin\n"
              << "  -h, --help         Show this help text\n\n"
              << "Controls:\n"
              << "  Arrow keys = directions, Z = Button1, X = Button2,\n"
              << "  Backspace = Meta1, Enter = Meta2, F1 = save state\n";
}

std::filesystem::file_time_type fileWriteTime(const std::filesystem::path& path) noexcept
{
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }
    return writeTime;
}

struct VisualReloadPollState {
    std::mutex mutex{};
    std::vector<std::filesystem::path> watchedPaths{};
    std::map<std::string, std::filesystem::file_time_type> lastWriteTimes{};
    std::atomic<bool> pollInFlight{false};
    std::atomic<bool> reloadRequested{false};
    std::chrono::steady_clock::time_point nextPollDue{};
};

std::string jsonEscape(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8u);
    for (const char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

void writeJsonDoubleOrNull(std::ostream& output, bool hasValue, double value)
{
    if (hasValue) {
        output << value;
    } else {
        output << "null";
    }
}

void populateAudioDiagnostics(BMMQ::FrontendStats& result,
                              const BMMQ::AudioTransportPluginStats& plugin,
                              const BMMQ::AudioService& service,
                              const BMMQ::IAudioOutputBackend* output) noexcept
{
    result.audioEvents = plugin.events;
    result.audioRealtimePacketsAccepted = plugin.realtimePacketsAccepted;
    result.audioRealtimePacketsSkipped = plugin.realtimePacketsSkipped;
    result.audioBatchFlushCount = plugin.batchFlushCount;
    result.audioBatchFlushSamplesLast = plugin.batchFlushSamplesLast;
    result.audioBatchFlushSamplesMin = plugin.batchFlushSamplesMin;
    result.audioBatchFlushSamplesMax = plugin.batchFlushSamplesMax;
    result.audioBatchPacketsAccumulated = plugin.batchPacketsAccumulated;
    result.audioBatchPacketsFlushed = plugin.batchPacketsFlushed;
    result.audioBatchCurrentSamples = plugin.batchCurrentSamples;
    result.audioRealtimePacketSamplesLast = plugin.packetSamplesLast;
    result.audioRealtimePacketSamplesMin = plugin.packetSamplesMin;
    result.audioRealtimePacketSamplesMax = plugin.packetSamplesMax;
    result.audioRealtimePacketSampleRateLast = plugin.sampleRateLast;
    result.audioRealtimePacketChannelCountLast = plugin.channelCountLast;
    result.audioRealtimePacketPsgChunksEmittedLast = plugin.psgChunksEmittedLast;
    result.audioRealtimePacketPsgSamplesGeneratedTotalLast =
        plugin.psgSamplesGeneratedTotalLast;
    result.audioRealtimePacketPsgChunkSamplesLast = plugin.psgChunkSamplesLast;
    result.audioRealtimePacketPsgChunkSamplesMin = plugin.psgChunkSamplesMin;
    result.audioRealtimePacketPsgChunkSamplesMax = plugin.psgChunkSamplesMax;
    result.audioRealtimePacketPsgPendingSamplesLast = plugin.psgPendingSamplesLast;
    const auto engine = service.engine().stats();
    const auto transport = service.transportStats();
    const auto device = output != nullptr ? output->deviceInfo() : BMMQ::AudioOutputDeviceInfo{};
    result.audioSourceSampleRate = service.engine().config().sourceSampleRate;
    result.audioDeviceSampleRate = service.engine().config().deviceSampleRate;
    result.audioRingBufferCapacitySamples = service.engine().bufferCapacitySamples();
    result.audioCallbackChunkSamples = device.callbackChunkSamples;
    result.audioBufferedHighWaterSamples = engine.bufferedHighWaterSamples;
    result.audioCallbackCount = engine.callbackCount;
    result.audioSamplesDelivered = engine.samplesDelivered;
    result.audioUnderrunCount = engine.underrunCount;
    result.audioSilenceSamplesFilled = engine.silenceSamplesFilled;
    result.audioOverrunDropCount = engine.overrunDropCount;
    result.audioDroppedSamples = engine.droppedSamples;
    result.audioResamplingActive = engine.resamplingActive;
    result.audioResampleRatio = engine.resampleRatio;
    result.audioSourceSamplesPushed = engine.sourceSamplesPushed;
    result.audioAppendCallCount = engine.appendCallCount;
    result.audioAppendSamplesRequested = engine.appendSamplesRequested;
    result.audioAppendSamplesAccepted = engine.appendSamplesAccepted;
    result.audioAppendSamplesRejected = engine.appendSamplesRejected;
    result.audioAppendSamplesTruncated = engine.appendSamplesTruncated;
    result.audioAppendBufferedSamplesLast = engine.appendBufferedSamplesLast;
    result.audioResampleSourceSamplesConsumed = engine.sourceSamplesConsumed;
    result.audioResampleOutputSamplesProduced = engine.outputSamplesProduced;
    result.audioPipelineCapacitySkipCount = engine.pipelineCapacitySkipCount;
    result.audioReadyQueueDepth = transport.readyQueueDepth;
    result.audioTransportConfiguredReadyQueueChunks = transport.configuredReadyQueueChunks;
    result.audioTransportPrefillTargetChunks = transport.prefillTargetChunks;
    result.audioTransportReadyQueueCapacityChunks = transport.readyQueueCapacityChunks;
    result.audioTransportReadyQueueUsableChunks = transport.readyQueueUsableChunks;
    result.audioReadyQueueHighWaterChunks = transport.readyQueueHighWaterChunks;
    result.audioReadyQueueLowWaterChunks = transport.readyQueueLowWaterChunks;
    result.audioReadyQueueEmptyCount = transport.readyQueueEmptyCount;
    result.audioTransportDrainCallbackCount = transport.drainCallbackCount;
    result.audioTransportDrainRequestedSamples = transport.drainRequestedSamples;
    result.audioTransportDrainReadySamples = transport.drainReadySamples;
    result.audioTransportUnderrunCount = transport.underrunCount;
    result.audioTransportSilenceSamplesFilled = transport.silenceSamplesFilled;
    result.audioTransportWorkerWakeCount = transport.workerWakeCount;
    result.audioTransportWorkerCallbackWakeCount = transport.workerCallbackWakeCount;
    result.audioTransportWorkerEmulationWakeCount = transport.workerEmulationWakeCount;
    result.audioTransportWorkerTimeoutWakeCount = transport.workerTimeoutWakeCount;
    result.audioTransportAppendRecentPcmCallCount = transport.appendRecentPcmCallCount;
    result.audioTransportAppendRecentPcmSamplesAppended = transport.appendRecentPcmSamplesAppended;
    result.audioTransportPrimedForDrain = transport.primedForDrain;
    result.audioTransportPrimedTransitionCount = transport.primedTransitionCount;
    result.audioTransportPrimingSilenceCallbackCount = transport.primingSilenceCallbackCount;
    result.audioTransportPrimingSilenceSamples = transport.primingSilenceSamples;
    result.audioTransportDrainDurationSampleCount = transport.drainCallbackDurationSampleCount;
    result.audioTransportDrainDurationLastNanos = transport.drainCallbackDurationLastNanos;
    result.audioTransportDrainDurationHighWaterNanos = transport.drainCallbackDurationHighWaterNanos;
    result.audioTransportDrainDurationP50Nanos = transport.drainCallbackDurationP50Nanos;
    result.audioTransportDrainDurationP95Nanos = transport.drainCallbackDurationP95Nanos;
    result.audioTransportDrainDurationP99Nanos = transport.drainCallbackDurationP99Nanos;
    result.audioTransportDrainDurationP999Nanos = transport.drainCallbackDurationP999Nanos;
    result.audioTransportDrainDurationUnder50usCount = transport.drainCallbackDurationUnder50usCount;
    result.audioTransportDrainDuration50To100usCount = transport.drainCallbackDuration50To100usCount;
    result.audioTransportDrainDuration100To250usCount = transport.drainCallbackDuration100To250usCount;
    result.audioTransportDrainDuration250To500usCount = transport.drainCallbackDuration250To500usCount;
    result.audioTransportDrainDuration500usTo1msCount = transport.drainCallbackDuration500usTo1msCount;
    result.audioTransportDrainDuration1To2msCount = transport.drainCallbackDuration1To2msCount;
    result.audioTransportDrainDuration2To5msCount = transport.drainCallbackDuration2To5msCount;
    result.audioTransportDrainDuration5To10msCount = transport.drainCallbackDuration5To10msCount;
    result.audioTransportDrainDurationOver10msCount = transport.drainCallbackDurationOver10msCount;
    result.audioTransportWorkerEmulationWakeLatencySampleCount =
        transport.workerEmulationWakeLatencySampleCount;
    result.audioTransportWorkerEmulationWakeLatencyLastNs =
        transport.workerEmulationWakeLatencyLastNs;
    result.audioTransportWorkerEmulationWakeLatencyHighWaterNs =
        transport.workerEmulationWakeLatencyHighWaterNs;
}

void writeDiagnosticsSample(std::ostream& output,
                            std::chrono::steady_clock::time_point startedAt,
                            std::chrono::steady_clock::time_point now,
                            std::uint64_t emulatedCycles,
                            std::uint64_t retiredInstructions,
                            std::uint32_t cpuClockHz,
                            const BMMQ::FrontendStats* frontendStats,
                            const BMMQ::TimingStats& timingStats,
                            const BMMQ::BackgroundTaskStats& backgroundStats,
                            const GameBoyMachine::BlockCacheStats* blockCacheStats,
                            bool detailedIrTimingEnabled,
                            const std::optional<std::string>& stateFingerprint,
                            std::string_view executorPolicyId,
                            BMMQ::ExecutionBackend executionBackend) noexcept
{
    using namespace std::chrono;
    const auto elapsedNs = duration_cast<nanoseconds>(now - startedAt).count();
    const auto elapsedSeconds = static_cast<double>(elapsedNs) / 1'000'000'000.0;
    const auto effectiveCyclesPerSecond =
        (elapsedSeconds > 0.0) ? (static_cast<double>(emulatedCycles) / elapsedSeconds) : 0.0;
    const auto effectiveSpeed =
        (cpuClockHz != 0u) ? (effectiveCyclesPerSecond / static_cast<double>(cpuClockHz)) : 0.0;

    const BMMQ::FrontendStats defaults{};
    const auto& stats = (frontendStats != nullptr) ? *frontendStats : defaults;
    const auto audioConfiguredChannelCount =
        static_cast<unsigned int>(stats.audioRealtimePacketChannelCountLast);
    const double audioExpectedSamplesPerSecond =
        (stats.audioSourceSampleRate > 0 && audioConfiguredChannelCount != 0u)
            ? static_cast<double>(stats.audioSourceSampleRate) *
                  static_cast<double>(audioConfiguredChannelCount)
            : 0.0;
    const auto audioKpi = BMMQ::classifyAudioKpiStatus(BMMQ::AudioKpiInputs{
        .elapsedSeconds = elapsedSeconds,
        .generatedSamples = stats.audioRealtimePacketPsgSamplesGeneratedTotalLast,
        .appendedSamples = stats.audioTransportAppendRecentPcmSamplesAppended,
        .expectedSamplesPerSecond = audioExpectedSamplesPerSecond,
        .drainedReadySamples = stats.audioTransportDrainReadySamples,
        .drainRequestedSamples = stats.audioTransportDrainRequestedSamples,
    });

    output << "{";
    output << "\"host_elapsed_ns\":" << elapsedNs;
    output << ",\"emulated_cycles\":" << emulatedCycles;
    output << ",\"retired_instructions\":" << retiredInstructions;
    output << ",\"deterministic_state\":{";
    output << "\"schema\":";
    if (blockCacheStats != nullptr) {
        output << "\"gameboy-v1\"";
    } else {
        output << "null";
    }
    output << ",\"fingerprint\":";
    if (stateFingerprint.has_value()) {
        output << '"' << jsonEscape(*stateFingerprint) << '"';
    } else {
        output << "null";
    }
    output << "}";
    output << ",\"effective_emulation_speed\":" << effectiveSpeed;
    output << ",\"effective_cycles_per_second\":" << effectiveCyclesPerSecond;
    output << ",\"active_timing_profile\":\""
           << jsonEscape(BMMQ::timingPolicyProfileName(timingStats.activeProfile)) << "\"";
    output << ",\"executor\":{";
    output << "\"policy_id\":\"" << jsonEscape(executorPolicyId) << "\"";
    output << ",\"backend\":\"" << BMMQ::executionBackendName(executionBackend) << "\"";
    output << "}";

    output << ",\"cpu_block_cache\":{";
    output << "\"supported\":" << (blockCacheStats != nullptr ? "true" : "false");
    output << ",\"mode\":\"" << BMMQ::executionBackendName(executionBackend) << "\"";
    output << ",\"detailed_timing_enabled\":"
           << (detailedIrTimingEnabled ? "true" : "false");
    if (blockCacheStats != nullptr) {
        output << ",\"hits\":" << blockCacheStats->hits.load();
        output << ",\"misses\":" << blockCacheStats->misses.load();
        output << ",\"translations\":" << blockCacheStats->translations.load();
        output << ",\"translated_instructions\":" << blockCacheStats->translatedInstructions.load();
        output << ",\"invalidations\":" << blockCacheStats->invalidations.load();
        output << ",\"guard_failures\":" << blockCacheStats->guardFailures.load();
        output << ",\"chain_continuations\":" << blockCacheStats->chainContinuations.load();
        output << ",\"unsupported_fallbacks\":" << blockCacheStats->unsupportedFallbacks.load();
        output << ",\"fast_eligibility_stops\":" << blockCacheStats->fastEligibilityStops.load();
        output << ",\"invalidation_requests\":" << blockCacheStats->invalidationRequests.load();
        output << ",\"invalidation_page_skips\":" << blockCacheStats->invalidationPageSkips.load();
        output << ",\"invalidation_scans\":" << blockCacheStats->invalidationScans.load();
        output << ",\"invalidation_blocks_examined\":"
               << blockCacheStats->invalidationBlocksExamined.load();
        const auto emitOpcodeCounts = [&](std::string_view name, const auto& counts) {
            output << ",\"" << name << "\":{";
            bool first = true;
            for (std::size_t opcode = 0u; opcode < counts.size(); ++opcode) {
                const auto count = counts[opcode].load();
                if (count == 0u) continue;
                if (!first) output << ',';
                first = false;
                output << '\"' << "0x" << std::uppercase << std::hex
                       << std::setw(2) << std::setfill('0') << opcode
                       << std::dec << std::nouppercase << "\":" << count;
            }
            output << '}';
        };
        emitOpcodeCounts("unsupported_fallback_opcodes",
                         blockCacheStats->unsupportedFallbackOpcodes);
        emitOpcodeCounts("fast_eligibility_stop_opcodes",
                         blockCacheStats->fastEligibilityStopOpcodes);
        output << ",\"ir_translations\":" << blockCacheStats->irTranslations.load();
        output << ",\"ir_executions\":" << blockCacheStats->irExecutions.load();
        output << ",\"ir_guard_failures\":" << blockCacheStats->irGuardFailures.load();
        output << ",\"ir_fallbacks\":" << blockCacheStats->irFallbacks.load();
        output << ",\"ir_lowered_instructions\":" << blockCacheStats->irLoweredInstructions.load();
        output << ",\"ir_ineligible_translations\":" << blockCacheStats->irIneligibleTranslations.load();
        output << ",\"ir_lowering_ns\":" << blockCacheStats->irLoweringNanos.load();
        output << ",\"ir_guard_checks\":" << blockCacheStats->irGuardChecks.load();
        output << ",\"ir_guard_check_ns\":" << blockCacheStats->irGuardCheckNanos.load();
        output << ",\"ir_execution_ns\":" << blockCacheStats->irExecutionNanos.load();
        output << ",\"ir_block_entries\":" << blockCacheStats->irBlockEntries.load();
        output << ",\"ir_block_continuations\":"
               << blockCacheStats->irBlockContinuations.load();
        output << ",\"ir_block_continuation_rejects\":"
               << blockCacheStats->irBlockContinuationRejects.load();
    }
    output << "}";

    output << ",\"render_service\":{";
    output << "\"state\":" << static_cast<unsigned int>(stats.renderServiceState);
    output << ",\"loop_count\":" << stats.renderServiceLoopCount;
    output << ",\"present_attempts\":" << stats.renderServicePresentAttempts;
    output << ",\"present_successes\":" << stats.renderServicePresentSuccessCount;
    output << ",\"present_failures\":" << stats.renderServicePresentFailureCount;
    output << ",\"event_pump_count\":" << stats.renderServiceEventPumpCount;
    output << ",\"frame_wake_count\":" << stats.renderServiceFrameWakeCount;
    output << ",\"timeout_wake_count\":" << stats.renderServiceTimeoutWakeCount;
    output << "}";

    output << ",\"video\":{";
    output << "\"frames_submitted\":" << stats.videoFramesPublished;
    output << ",\"frames_presented\":" << stats.framesPresented;
    output << ",\"realtime_render_request_count\":" << stats.videoRealtimeRenderRequestCount;
    output << ",\"debug_render_request_count\":" << stats.videoDebugRenderRequestCount;
    output << ",\"realtime_render_request_from_vblank_count\":" << stats.videoRealtimeRenderFromVBlankCount;
    output << ",\"realtime_render_request_from_scanline_count\":" << stats.videoRealtimeRenderFromScanlineCount;
    output << ",\"realtime_render_request_from_memory_write_count\":" << stats.videoRealtimeRenderFromMemoryWriteCount;
    output << ",\"realtime_render_request_from_other_count\":" << stats.videoRealtimeRenderFromOtherCount;
    output << ",\"debug_render_request_from_vblank_count\":" << stats.videoDebugRenderFromVBlankCount;
    output << ",\"debug_render_request_from_scanline_count\":" << stats.videoDebugRenderFromScanlineCount;
    output << ",\"debug_render_request_from_memory_write_count\":" << stats.videoDebugRenderFromMemoryWriteCount;
    output << ",\"debug_render_request_from_other_count\":" << stats.videoDebugRenderFromOtherCount;
    output << ",\"debug_model_build_skip_count\":" << stats.videoDebugModelBuildSkipCount;
    output << ",\"build_debug_frame\":{";
    output << "\"call_count\":" << stats.videoBuildDebugFrameCallCount;
    output << ",\"total_ns\":" << stats.videoBuildDebugFrameTotalNs;
    output << ",\"realtime_reason_count\":" << stats.videoBuildDebugFrameRealtimeReasonCount;
    output << ",\"debug_reason_count\":" << stats.videoBuildDebugFrameDebugReasonCount;
    output << ",\"fallback_reason_count\":" << stats.videoBuildDebugFrameFallbackReasonCount;
    output << ",\"unknown_reason_count\":" << stats.videoBuildDebugFrameUnknownReasonCount;
    output << ",\"debug_consumer_active_count\":" << stats.videoBuildDebugFrameDebugConsumerActiveCount;
    output << ",\"debug_consumer_inactive_count\":" << stats.videoBuildDebugFrameDebugConsumerInactiveCount;
    output << ",\"skipped_no_consumer_count\":" << stats.videoDebugFrameBuildSkippedNoConsumerCount;
    output << ",\"executed_count\":" << stats.videoDebugFrameBuildExecutedCount;
    output << ",\"visual_override_lookup_samples\":" << stats.videoVisualOverrideLookupSampleCount;
    output << ",\"visual_override_lookup_total_ns\":" << stats.videoVisualOverrideLookupTotalNs;
    output << ",\"visual_override_lookup_high_water_ns\":" << stats.videoVisualOverrideLookupHighWaterNs;
    output << ",\"visual_override_apply_samples\":" << stats.videoVisualOverrideApplySampleCount;
    output << ",\"visual_override_apply_total_ns\":" << stats.videoVisualOverrideApplyTotalNs;
    output << ",\"visual_override_apply_high_water_ns\":" << stats.videoVisualOverrideApplyHighWaterNs;
    output << "}";
    output << ",\"vdp_render_body\":{";
    output << "\"sample_count\":" << stats.videoVdpRenderBodySampleCount;
    output << ",\"total_ns\":" << stats.videoVdpRenderBodyTotalNs;
    output << ",\"setup_ns\":" << stats.videoVdpRenderBodySetupNs;
    output << ",\"background_ns\":" << stats.videoVdpRenderBodyBackgroundNs;
    output << ",\"background_simple_ns\":" << stats.videoVdpRenderBodyBackgroundSimpleNs;
    output << ",\"background_general_ns\":" << stats.videoVdpRenderBodyBackgroundGeneralNs;
    output << ",\"background_tms_ns\":" << stats.videoVdpRenderBodyBackgroundTmsNs;
    output << ",\"sprite_probe_ns\":" << stats.videoVdpRenderBodySpriteProbeNs;
    output << ",\"sprite_overlay_ns\":" << stats.videoVdpRenderBodySpriteOverlayNs;
    output << ",\"other_ns\":" << stats.videoVdpRenderBodyOtherNs;
    output << ",\"simple_bitplane_decode_ns\":" << stats.videoVdpSimpleBitplaneDecodeNs;
    output << ",\"palette_framebuffer_write_ns\":" << stats.videoVdpSimplePaletteFbWriteNs;
    output << ",\"simple_loop_other_ns\":" << stats.videoVdpSimpleLoopOtherNs;
    output << "}";
    output << ",\"vdp_mode4_background_attributes\":{";
    output << "\"tile_cells_processed\":" << stats.videoVdpMode4AttrTileCellsProcessed;
    output << ",\"tile_cells_flip_h\":" << stats.videoVdpMode4AttrTileCellsFlipH;
    output << ",\"tile_cells_flip_v\":" << stats.videoVdpMode4AttrTileCellsFlipV;
    output << ",\"tile_cells_palette1\":" << stats.videoVdpMode4AttrTileCellsPalette1;
    output << ",\"tile_cells_priority\":" << stats.videoVdpMode4AttrTileCellsPriority;
    output << ",\"tile_cells_fixed_top_rows\":" << stats.videoVdpMode4AttrTileCellsFixedTopRows;
    output << ",\"tile_cells_fixed_right_columns\":" << stats.videoVdpMode4AttrTileCellsFixedRightColumns;
    output << ",\"tile_cells_left_blank_or_fine_skip\":" << stats.videoVdpMode4AttrTileCellsLeftBlankOrFineSkip;
    output << ",\"tile_cells_common_case_eligible\":" << stats.videoVdpMode4AttrTileCellsCommonCaseEligible;
    output << ",\"common_case_eligible_pixels_written\":"
           << stats.videoVdpMode4AttrCommonCaseEligiblePixelsWritten;
    output << "}";
    output << ",\"vdp_mode4_simple_background\":{";
    output << "\"simple_path_frame_count\":" << stats.videoVdpMode4SimplePathFrameCount;
    output << ",\"simple_path_rows_rendered\":" << stats.videoVdpMode4SimplePathRowsRendered;
    output << ",\"simple_path_pixels_written\":" << stats.videoVdpMode4SimplePathPixelsWritten;
    output << ",\"simple_path_tile_entries_decoded\":" << stats.videoVdpMode4SimplePathTileEntriesDecoded;
    output << ",\"simple_path_pattern_rows_decoded\":" << stats.videoVdpMode4SimplePathPatternRowsDecoded;
    output << ",\"simple_path_scroll_x_aligned_count\":" << stats.videoVdpMode4SimplePathScrollXAlignedCount;
    output << ",\"simple_path_scroll_y_value_changes\":" << stats.videoVdpMode4SimplePathScrollYValueChanges;
    output << ",\"simple_path_unique_tile_rows_seen\":" << stats.videoVdpMode4SimplePathUniqueTileRowsSeen;
    output << ",\"mode4_simple_path_used_count\":" << stats.videoVdpMode4SimplePathUsedCount;
    output << ",\"mode4_general_path_used_count\":" << stats.videoVdpMode4GeneralPathUsedCount;
    output << ",\"tms_graphics_path_used_count\":" << stats.videoTmsGraphicsPathUsedCount;
    output << "}";
    // T2.3 Phase 3: frame build duration histogram (separate from presenter present)
    output << ",\"frame_build_duration\":{";
    output << "\"sample_count\":" << stats.videoFrameBuildDurationSampleCount;
    output << ",\"last_ns\":" << stats.videoFrameBuildDurationLastNanos;
    output << ",\"high_water_ns\":" << stats.videoFrameBuildDurationHighWaterNanos;
    output << ",\"under_50us\":" << stats.videoFrameBuildDurationUnder50usCount;
    output << ",\"_50_to_100us\":" << stats.videoFrameBuildDuration50To100usCount;
    output << ",\"_100_to_250us\":" << stats.videoFrameBuildDuration100To250usCount;
    output << ",\"_250_to_500us\":" << stats.videoFrameBuildDuration250To500usCount;
    output << ",\"_500us_to_1ms\":" << stats.videoFrameBuildDuration500usTo1msCount;
    output << ",\"_1_to_2ms\":" << stats.videoFrameBuildDuration1To2msCount;
    output << ",\"_2_to_5ms\":" << stats.videoFrameBuildDuration2To5msCount;
    output << ",\"_5_to_10ms\":" << stats.videoFrameBuildDuration5To10msCount;
    output << ",\"over_10ms\":" << stats.videoFrameBuildDurationOver10msCount;
    output << "}";
    output << ",\"fresh_presents\":" << stats.videoPresentFreshFrameCount;
    output << ",\"fallback_presents\":" << stats.videoPresentFallbackCount;
    output << ",\"mailbox_overwrites\":" << stats.videoMailboxOverwriteCount;
    output << ",\"publish_to_present_age\":{";
    output << "\"last_ns\":" << stats.videoFrameAgeLastNs;
    output << ",\"high_water_ns\":" << stats.videoFrameAgeHighWaterNs;
    output << ",\"under_50us\":" << stats.videoFrameAgeUnder50usCount;
    output << ",\"_50_to_100us\":" << stats.videoFrameAge50To100usCount;
    output << ",\"_100_to_250us\":" << stats.videoFrameAge100To250usCount;
    output << ",\"_250_to_500us\":" << stats.videoFrameAge250To500usCount;
    output << ",\"_500us_to_1ms\":" << stats.videoFrameAge500usTo1msCount;
    output << ",\"_1_to_2ms\":" << stats.videoFrameAge1To2msCount;
    output << ",\"_2_to_5ms\":" << stats.videoFrameAge2To5msCount;
    output << ",\"_5_to_10ms\":" << stats.videoFrameAge5To10msCount;
    output << ",\"over_10ms\":" << stats.videoFrameAgeOver10msCount;
    output << "}";
    output << ",\"presenter_duration\":{";
    output << "\"sample_count\":" << stats.videoPresenterPresentDurationSampleCount;
    output << ",\"last_ns\":" << stats.videoPresenterPresentDurationLastNanos;
    output << ",\"high_water_ns\":" << stats.videoPresenterPresentDurationHighWaterNanos;
    output << ",\"p50_ns\":" << stats.videoPresenterPresentDurationP50Nanos;
    output << ",\"p95_ns\":" << stats.videoPresenterPresentDurationP95Nanos;
    output << ",\"p99_ns\":" << stats.videoPresenterPresentDurationP99Nanos;
    output << ",\"p999_ns\":" << stats.videoPresenterPresentDurationP999Nanos;
    output << ",\"under_50us\":" << stats.videoPresenterPresentDurationUnder50usCount;
    output << ",\"_50_to_100us\":" << stats.videoPresenterPresentDuration50To100usCount;
    output << ",\"_100_to_250us\":" << stats.videoPresenterPresentDuration100To250usCount;
    output << ",\"_250_to_500us\":" << stats.videoPresenterPresentDuration250To500usCount;
    output << ",\"_500us_to_1ms\":" << stats.videoPresenterPresentDuration500usTo1msCount;
    output << ",\"_1_to_2ms\":" << stats.videoPresenterPresentDuration1To2msCount;
    output << ",\"_2_to_5ms\":" << stats.videoPresenterPresentDuration2To5msCount;
    output << ",\"_5_to_10ms\":" << stats.videoPresenterPresentDuration5To10msCount;
    output << ",\"over_10ms\":" << stats.videoPresenterPresentDurationOver10msCount;
    output << "}";
    output << ",\"presenter_pipeline\":{";
    output << "\"sample_count\":" << stats.videoPresenterStageDurationSampleCount;
    output << ",\"simd_backend\":\"" << jsonEscape(stats.videoSimdBackendName) << "\"";
    output << ",\"direct_indexed_frames\":" << stats.videoPresenterDirectIndexedFrameCount;
    output << ",\"argb_frames\":" << stats.videoPresenterArgbFrameCount;
    output << ",\"texture_locks\":" << stats.videoPresenterTextureLockCount;
    output << ",\"renderer_flags\":" << stats.videoPresenterRendererFlags;
    output << ",\"renderer_accelerated\":"
           << (stats.videoPresenterRendererAccelerated ? "true" : "false");
    output << ",\"render_target_supported\":"
           << (stats.videoPresenterRenderTargetSupported ? "true" : "false");
    output << ",\"expansion_last_ns\":" << stats.videoPresenterExpansionDurationLastNanos;
    output << ",\"expansion_high_water_ns\":"
           << stats.videoPresenterExpansionDurationHighWaterNanos;
    output << ",\"upload_last_ns\":" << stats.videoPresenterUploadDurationLastNanos;
    output << ",\"upload_high_water_ns\":" << stats.videoPresenterUploadDurationHighWaterNanos;
    output << ",\"render_submit_last_ns\":"
           << stats.videoPresenterRenderSubmitDurationLastNanos;
    output << ",\"render_submit_high_water_ns\":"
           << stats.videoPresenterRenderSubmitDurationHighWaterNanos;
    output << ",\"total_last_ns\":" << stats.videoPresenterTotalDurationLastNanos;
    output << ",\"total_high_water_ns\":" << stats.videoPresenterTotalDurationHighWaterNanos;
    output << "}";
    output << "}";

    output << ",\"audio\":{";
    output << "\"primed_for_drain\":" << (stats.audioTransportPrimedForDrain ? "true" : "false");
    output << ",\"primed_transition_count\":" << stats.audioTransportPrimedTransitionCount;
    output << ",\"priming_silence_callback_count\":"
           << stats.audioTransportPrimingSilenceCallbackCount;
    output << ",\"priming_silence_samples\":" << stats.audioTransportPrimingSilenceSamples;
    output << ",\"kpi_status\":\"" << BMMQ::audioKpiStatusName(audioKpi.status) << "\"";
    output << ",\"kpi_source_ratio\":";
    writeJsonDoubleOrNull(output, audioKpi.hasSourceRatio, audioKpi.sourceRatio);
    output << ",\"kpi_drain_ratio\":";
    writeJsonDoubleOrNull(output, audioKpi.hasDrainRatio, audioKpi.drainRatio);
    output << ",\"kpi_thresholds\":{";
    output << "\"source_realtime_threshold\":" << BMMQ::kAudioKpiSourceRealtimeThreshold;
    output << ",\"drain_realtime_threshold\":" << BMMQ::kAudioKpiDrainRealtimeThreshold;
    output << ",\"source_drain_close_tolerance\":" << BMMQ::kAudioKpiSourceDrainCloseTolerance;
    output << "}";
    output << ",\"underruns_after_priming\":" << stats.audioTransportUnderrunCount;
    output << ",\"silence_samples_after_priming\":" << stats.audioTransportSilenceSamplesFilled;
    output << ",\"config\":{";
    const auto frameChunkSamples = stats.audioRealtimePacketSamplesLast;
    const auto ringCapacitySamples = stats.audioRingBufferCapacitySamples;
    const auto ringCapacityChunks =
        frameChunkSamples != 0u ? (ringCapacitySamples / frameChunkSamples) : 0u;
    output << "\"ring_capacity_samples\":" << ringCapacitySamples;
    output << ",\"ring_capacity_chunks\":" << ringCapacityChunks;
    output << ",\"frame_chunk_samples\":" << frameChunkSamples;
    output << ",\"callback_requested_samples_last\":" << stats.audioCallbackChunkSamples;
    output << ",\"callback_requested_samples_min\":" << stats.audioCallbackChunkSamples;
    output << ",\"callback_requested_samples_max\":" << stats.audioCallbackChunkSamples;
    output << ",\"configured_sample_rate\":" << stats.audioSourceSampleRate;
    output << ",\"configured_channel_count\":" << audioConfiguredChannelCount;
    output << "}";
    output << ",\"ready_queue\":{";
    output << "\"configured_chunks\":" << stats.audioTransportConfiguredReadyQueueChunks;
    output << ",\"prefill_target_chunks\":" << stats.audioTransportPrefillTargetChunks;
    output << ",\"capacity_chunks\":" << stats.audioTransportReadyQueueCapacityChunks;
    output << ",\"usable_chunks\":" << stats.audioTransportReadyQueueUsableChunks;
    output << ",\"depth_last\":" << stats.audioReadyQueueDepth;
    output << ",\"depth_high_water\":" << stats.audioReadyQueueHighWaterChunks;
    output << ",\"depth_low_water\":" << stats.audioReadyQueueLowWaterChunks;
    output << ",\"empty_count\":" << stats.audioReadyQueueEmptyCount;
    output << ",\"drop_count\":" << stats.audioTransportDroppedReadyBlocks;
    // T2.2 Phase 2: FIFO occupancy at drain callback tick
    output << ",\"drain_occupancy\":{";
    output << "\"depth_last\":" << stats.audioReadyQueueDrainOccupancyDepthLast;
    output << ",\"depth_high_water\":" << stats.audioReadyQueueDrainOccupancyHighWaterChunks;
    output << ",\"depth_low_water\":" << stats.audioReadyQueueDrainOccupancyLowWaterChunks;
    output << ",\"sample_count\":" << stats.audioReadyQueueDrainOccupancySampleCount;
    output << ",\"bucket_0_empty\":" << stats.audioReadyQueueDrainOccupancyUnder1ChunkCount;
    output << ",\"bucket_1\":" << stats.audioReadyQueueDrainOccupancy1ChunkCount;
    output << ",\"bucket_2\":" << stats.audioReadyQueueDrainOccupancy2ChunksCount;
    output << ",\"bucket_3\":" << stats.audioReadyQueueDrainOccupancy3ChunksCount;
    output << ",\"bucket_4\":" << stats.audioReadyQueueDrainOccupancy4ChunksCount;
    output << ",\"bucket_5_plus\":" << stats.audioReadyQueueDrainOccupancy5PlusChunksCount;
    output << "}";
    output << "}";
    output << ",\"worker\":{";
    output << "\"wake_count\":" << stats.audioTransportWorkerWakeCount;
    output << ",\"callback_wake_count\":" << stats.audioTransportWorkerCallbackWakeCount;
    output << ",\"emulation_wake_count\":" << stats.audioTransportWorkerEmulationWakeCount;
    output << ",\"timeout_wake_count\":" << stats.audioTransportWorkerTimeoutWakeCount;
    output << ",\"loop_iterations\":" << stats.audioTransportWorkerLoopIterations;
    output << ",\"wake_produced_blocks_0\":" << stats.audioTransportWorkerWakeProducedBlocks0Count;
    output << ",\"wake_produced_blocks_1\":" << stats.audioTransportWorkerWakeProducedBlocks1Count;
    output << ",\"wake_produced_blocks_2\":" << stats.audioTransportWorkerWakeProducedBlocks2Count;
    output << ",\"wake_produced_blocks_3_plus\":" << stats.audioTransportWorkerWakeProducedBlocks3PlusCount;
    output << ",\"wake_period_ms\":" << stats.audioTransportWorkerWakePeriodMilliseconds;
    output << ",\"production_attempts\":" << stats.audioTransportWorkerProductionAttempts;
    output << ",\"production_source_empty_failures\":"
           << stats.audioTransportWorkerProductionSourceEmptyFailures;
    output << ",\"production_ready_queue_full_failures\":"
           << stats.audioTransportWorkerProductionReadyQueueFullFailures;
    output << ",\"produced_blocks\":" << stats.audioTransportWorkerProducedBlocks;
    output << ",\"source_buffered_samples_last\":"
           << stats.audioTransportWorkerWakeSourceBufferedSamplesLast;
    output << ",\"source_buffered_samples_high_water\":"
           << stats.audioTransportWorkerWakeSourceBufferedSamplesHighWater;
    output << ",\"source_buffered_samples_low_water\":"
           << stats.audioTransportWorkerWakeSourceBufferedSamplesLowWater;
    output << ",\"source_buffered_samples_on_success_last\":"
           << stats.audioTransportWorkerProductionSourceBufferedSamplesSuccessLast;
    output << ",\"source_buffered_samples_on_failure_last\":"
           << stats.audioTransportWorkerProductionSourceBufferedSamplesFailureLast;
    output << "}";
    output << ",\"drain\":{";
    output << "\"callback_count\":" << stats.audioTransportDrainCallbackCount;
    output << ",\"requested_samples_total\":" << stats.audioTransportDrainRequestedSamples;
    output << ",\"drained_ready_samples_total\":" << stats.audioTransportDrainReadySamples;
    output << ",\"underrun_count\":" << stats.audioTransportUnderrunCount;
    output << ",\"silence_samples_filled\":" << stats.audioTransportSilenceSamplesFilled;
    output << ",\"duration\":{";
    output << "\"sample_count\":" << stats.audioTransportDrainDurationSampleCount;
    output << ",\"last_ns\":" << stats.audioTransportDrainDurationLastNanos;
    output << ",\"high_water_ns\":" << stats.audioTransportDrainDurationHighWaterNanos;
    output << ",\"p50_ns\":" << stats.audioTransportDrainDurationP50Nanos;
    output << ",\"p95_ns\":" << stats.audioTransportDrainDurationP95Nanos;
    output << ",\"p99_ns\":" << stats.audioTransportDrainDurationP99Nanos;
    output << ",\"p999_ns\":" << stats.audioTransportDrainDurationP999Nanos;
    output << ",\"under_50us\":" << stats.audioTransportDrainDurationUnder50usCount;
    output << ",\"_50_to_100us\":" << stats.audioTransportDrainDuration50To100usCount;
    output << ",\"_100_to_250us\":" << stats.audioTransportDrainDuration100To250usCount;
    output << ",\"_250_to_500us\":" << stats.audioTransportDrainDuration250To500usCount;
    output << ",\"_500us_to_1ms\":" << stats.audioTransportDrainDuration500usTo1msCount;
    output << ",\"_1_to_2ms\":" << stats.audioTransportDrainDuration1To2msCount;
    output << ",\"_2_to_5ms\":" << stats.audioTransportDrainDuration2To5msCount;
    output << ",\"_5_to_10ms\":" << stats.audioTransportDrainDuration5To10msCount;
    output << ",\"over_10ms\":" << stats.audioTransportDrainDurationOver10msCount;
    output << "}";
    output << "}";
    output << ",\"append_recent_pcm\":{";
    output << "\"call_count\":" << stats.audioTransportAppendRecentPcmCallCount;
    output << ",\"samples_appended_total\":" << stats.audioTransportAppendRecentPcmSamplesAppended;
    output << ",\"engine_append_call_count\":" << stats.audioAppendCallCount;
    output << ",\"engine_samples_requested_total\":" << stats.audioAppendSamplesRequested;
    output << ",\"engine_samples_accepted_total\":" << stats.audioAppendSamplesAccepted;
    output << ",\"engine_samples_rejected_total\":" << stats.audioAppendSamplesRejected;
    output << ",\"engine_samples_truncated_total\":" << stats.audioAppendSamplesTruncated;
    output << ",\"engine_buffered_samples_after_append_last\":" << stats.audioAppendBufferedSamplesLast;
    output << "}";
    output << ",\"source_packet\":{";
    output << "\"realtime_packets_accepted\":" << stats.audioRealtimePacketsAccepted;
    output << ",\"samples_last\":" << stats.audioRealtimePacketSamplesLast;
    output << ",\"samples_min\":" << stats.audioRealtimePacketSamplesMin;
    output << ",\"samples_max\":" << stats.audioRealtimePacketSamplesMax;
    output << ",\"sample_rate_last\":" << stats.audioRealtimePacketSampleRateLast;
    output << ",\"channel_count_last\":" << static_cast<unsigned int>(stats.audioRealtimePacketChannelCountLast);
    output << ",\"psg_chunks_emitted_last\":" << stats.audioRealtimePacketPsgChunksEmittedLast;
    output << ",\"psg_samples_generated_total_last\":" << stats.audioRealtimePacketPsgSamplesGeneratedTotalLast;
    output << ",\"psg_chunk_samples_last\":" << stats.audioRealtimePacketPsgChunkSamplesLast;
    output << ",\"psg_chunk_samples_min\":" << stats.audioRealtimePacketPsgChunkSamplesMin;
    output << ",\"psg_chunk_samples_max\":" << stats.audioRealtimePacketPsgChunkSamplesMax;
    output << ",\"psg_pending_samples_last\":" << stats.audioRealtimePacketPsgPendingSamplesLast;
    output << "}";
    output << ",\"batching\":{";
    output << "\"configured_chunks\":" << stats.audioBatchConfiguredChunks;
    output << ",\"flush_count\":" << stats.audioBatchFlushCount;
    output << ",\"flush_samples_last\":" << stats.audioBatchFlushSamplesLast;
    output << ",\"flush_samples_min\":" << stats.audioBatchFlushSamplesMin;
    output << ",\"flush_samples_max\":" << stats.audioBatchFlushSamplesMax;
    output << ",\"packets_accumulated\":" << stats.audioBatchPacketsAccumulated;
    output << ",\"packets_flushed\":" << stats.audioBatchPacketsFlushed;
    output << ",\"flush_reason_target_count\":" << stats.audioBatchFlushReasonTargetCount;
    output << ",\"flush_reason_format_change_count\":" << stats.audioBatchFlushReasonFormatChangeCount;
    output << ",\"flush_reason_lifecycle_count\":" << stats.audioBatchFlushReasonLifecycleCount;
    output << ",\"current_samples\":" << stats.audioBatchCurrentSamples;
    output << "}";
    output << ",\"worker_wake_latency\":{";
    output << "\"sample_count\":" << stats.audioTransportWorkerEmulationWakeLatencySampleCount;
    output << ",\"last_ns\":" << stats.audioTransportWorkerEmulationWakeLatencyLastNs;
    output << ",\"high_water_ns\":" << stats.audioTransportWorkerEmulationWakeLatencyHighWaterNs;
    output << ",\"under_100us\":" << stats.audioTransportWorkerEmulationWakeLatencyUnder100usCount;
    output << ",\"_100_to_500us\":" << stats.audioTransportWorkerEmulationWakeLatency100To500usCount;
    output << ",\"_500us_to_1ms\":" << stats.audioTransportWorkerEmulationWakeLatency500usTo1msCount;
    output << ",\"_1_to_2ms\":" << stats.audioTransportWorkerEmulationWakeLatency1To2msCount;
    output << ",\"_2_to_5ms\":" << stats.audioTransportWorkerEmulationWakeLatency2To5msCount;
    output << ",\"_5_to_10ms\":" << stats.audioTransportWorkerEmulationWakeLatency5To10msCount;
    output << ",\"_10_to_20ms\":" << stats.audioTransportWorkerEmulationWakeLatency10To20msCount;
    output << ",\"over_20ms\":" << stats.audioTransportWorkerEmulationWakeLatencyOver20msCount;
    output << "}";
    output << "}";

    output << ",\"background_tasks\":";
    BMMQ::writeBackgroundTaskDiagnosticsJson(output, backgroundStats);

    // TimingService is authoritative in headless and SDL modes. Do not source
    // these fields from the frontend's asynchronously mirrored stats snapshot.
    output << ",\"timing\":";
    BMMQ::writeTimingDiagnosticsJson(output, timingStats);
    output << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto parsedArguments = BMMQ::parseEmulatorArguments(argc, argv);
        if (parsedArguments.helpRequested) {
            printUsage((argc > 0 && argv != nullptr) ? argv[0] : "timeEmulator");
            return EXIT_SUCCESS;
        }
        const auto options = BMMQ::resolveEmulatorConfig(parsedArguments);

        std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
        std::signal(SIGTERM, handleSignal);
#endif

        const std::optional<std::size_t> backgroundWorkers = options.backgroundWorkers == 0u
            ? std::nullopt
            : std::optional<std::size_t>(options.backgroundWorkers);
        BMMQ::BackgroundTaskService backgroundTaskService(
            static_cast<std::size_t>(options.backgroundQueueCapacity), backgroundWorkers);
        backgroundTaskService.start();
        BMMQ::ImageDecoder imageDecoder(&backgroundTaskService);
        BMMQ::DebugSnapshotService debugSnapshotService;
        debugSnapshotService.setBackgroundTaskService(&backgroundTaskService);

        auto bootstrapped = BMMQ::bootstrapMachine(options);
        auto& machine = *bootstrapped.machine;
        const auto& descriptor = bootstrapped.descriptor;
        const auto romSize = bootstrapped.romSize;
        std::optional<BMMQ::Plugin::DynamicPluginModule> executorModule;
        std::optional<BMMQ::Plugin::DynamicPluginModule> irAdapterModule;
        std::optional<BMMQ::Plugin::DynamicPluginModule> irBackendModule;
        std::unique_ptr<BMMQ::Plugin::IExecutorPolicyPlugin> executorPolicy;
        if (options.executorPluginPath.has_value()) {
            executorModule = BMMQ::Plugin::DynamicPluginModule::load(*options.executorPluginPath);
            const auto ids = executorModule->executorPolicyIds();
            const auto selectedId = options.executorPolicyId.has_value()
                ? *options.executorPolicyId
                : (ids.size() == 1u ? ids.front() : std::string{});
            if (selectedId.empty()) {
                throw std::invalid_argument(
                    "--executor-policy is required when a module exposes multiple policies");
            }
            executorPolicy = executorModule->createExecutorPolicy(selectedId);
        } else {
            const auto executorPolicyId = options.executorPolicyId.has_value()
                ? std::string_view(*options.executorPolicyId)
                : BMMQ::Plugin::executorPolicyIdForLegacyMode(options.cpuMode);
            executorPolicy = BMMQ::Plugin::ExecutorPolicyRegistry::builtins().create(executorPolicyId);
        }
        machine.attachExecutorPolicy(*executorPolicy);
        if (options.irAdapterPluginPath.has_value() ||
            options.irBackendPluginPath.has_value()) {
            auto* irMachine = dynamic_cast<BMMQ::IIrComponentAwareMachine*>(&machine);
            if (irMachine == nullptr) {
                throw std::invalid_argument(
                    "selected machine does not support configurable IR components");
            }

            std::unique_ptr<BMMQ::IR::IIrCoreAdapter> adapter;
            std::unique_ptr<BMMQ::IR::IIrExecutionBackend> backend;
            std::string componentError;
            if (options.irAdapterPluginPath.has_value()) {
                irAdapterModule = BMMQ::Plugin::DynamicPluginModule::load(
                    *options.irAdapterPluginPath);
                adapter = irAdapterModule->createIrCoreAdapter(
                    *options.irAdapterId, &componentError);
                if (!adapter) {
                    throw std::invalid_argument(componentError.empty()
                        ? "unable to create selected IR core adapter"
                        : componentError);
                }
                if (adapter->architectureId() != irMachine->irArchitectureId()) {
                    throw std::invalid_argument(
                        "selected IR core adapter does not match the active machine");
                }
            }
            if (options.irBackendPluginPath.has_value()) {
                componentError.clear();
                irBackendModule = BMMQ::Plugin::DynamicPluginModule::load(
                    *options.irBackendPluginPath);
                backend = irBackendModule->createIrExecutionBackend(
                    *options.irBackendId, &componentError);
                if (!backend) {
                    throw std::invalid_argument(componentError.empty()
                        ? "unable to create selected IR execution backend"
                        : componentError);
                }
            }
            irMachine->setIrComponents(std::move(adapter), std::move(backend));
        }
        const auto& activeExecutorPolicy = machine.attachedExecutorPolicy();
        if (options.cpuDetailedTiming) {
            auto* gameBoyMachine = dynamic_cast<GameBoyMachine*>(bootstrapped.machine.get());
            if (gameBoyMachine == nullptr) {
                throw std::runtime_error("detailed IR timing requires the Game Boy core");
            }
            gameBoyMachine->setDetailedIrTimingEnabled(options.cpuDetailedTiming);
        }
        machine.videoService().setBackgroundTaskService(&backgroundTaskService);
        machine.visualOverrideService().setBackgroundTaskService(&backgroundTaskService);
        machine.visualOverrideService().setImageDecoder(&imageDecoder);
        // Note: bootstrapMachine now configures hdScale into VideoService.
        // Frontend propagation is handled via FrontendConfig.hdScale below.
        if (auto* gameBoyMachine = dynamic_cast<GameBoyMachine*>(bootstrapped.machine.get());
            gameBoyMachine != nullptr) {
            gameBoyMachine->setBackgroundTaskService(&backgroundTaskService);
        }
        if (auto* gameGearMachine = dynamic_cast<BMMQ::GameGearMachine*>(bootstrapped.machine.get());
            gameGearMachine != nullptr) {
            gameGearMachine->setBackgroundTaskService(&backgroundTaskService);
        }

        BMMQ::IFrontendPlugin* frontend = nullptr;
        std::unique_ptr<BMMQ::IFrontendPlugin> frontendPlugin;
        if (!options.headless) {
            BMMQ::FrontendConfig config;
            config.windowTitle = "Proto-Time - " + std::string(descriptor.displayName) +
                " - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1u);
            config.hdScale = std::clamp(options.hdScale, 1u, 8u);
            config.frameWidth = descriptor.defaultFrameWidth;
            config.frameHeight = descriptor.defaultFrameHeight;
            config.autoInitializeBackend = true;
            // Window/event APIs are host-main-thread-affine. The process main
            // thread is the render lane; guest execution runs separately.
            config.enableRenderServiceThread = false;
            // Present automatically so the window appears during normal runs.
            config.createHiddenWindowOnInitialize = false;
            config.pumpBackendEventsOnInputSample = false;
            config.autoPresentOnVideoEvent = true;
            config.showWindowOnPresent = true;
            config.retainDebugSnapshots = options.debugSnapshotsEnabled;
            const auto selectedFrontendId = BMMQ::normalizeFrontendId(
                options.frontendId.value_or(std::string{}));
            const auto defaultFilename = BMMQ::defaultFrontendPluginFilename(
                selectedFrontendId);
            const auto executablePath = (argc > 0 && argv != nullptr)
                ? std::filesystem::path(argv[0])
                : std::filesystem::path("timeEmulator");
            const auto pluginPath = options.pluginPath.value_or(
                BMMQ::defaultFrontendPluginPath(executablePath, defaultFilename));
            try {
                frontendPlugin = BMMQ::loadFrontendPlugin(
                    pluginPath, config, selectedFrontendId);
                frontend = frontendPlugin.get();
                if (options.debugSnapshotsEnabled) {
                    frontend->setDebugSnapshotService(&debugSnapshotService);
                }
                machine.pluginManager().add(std::move(frontendPlugin));
            } catch (const std::exception& ex) {
                std::cerr << "warning: " << ex.what() << "; continuing headless\n";
            }
        }

        BMMQ::AudioTransportPlugin* audioTransport = nullptr;
        if (options.audioEnabled) {
            auto transport = std::make_unique<BMMQ::AudioTransportPlugin>(
                static_cast<std::size_t>(std::clamp<std::uint32_t>(
                    options.audioBatchChunks, 1u, 16u)));
            audioTransport = transport.get();
            machine.pluginManager().add(std::move(transport));
        }
        if (options.midiOutputFilePath.has_value()) {
            auto sink = std::make_unique<BMMQ::MidiFileSink>(*options.midiOutputFilePath);
            machine.pluginManager().add(std::make_unique<BMMQ::PsgMidiPlugin>(std::move(sink)));
            std::cout << "MIDI output: " << *options.midiOutputFilePath << '\n';
        }
#if defined(BMMQ_HAS_ALSA_MIDI)
        if (options.midiOutputPort.has_value()) {
            BMMQ::AlsaMidiSinkConfig midiConfig;
            if (*options.midiOutputPort != "subscribers") {
                midiConfig.destination = *options.midiOutputPort;
            }
            auto alsaSink = std::make_unique<BMMQ::AlsaMidiSink>(std::move(midiConfig));
            const auto sourceAddress = alsaSink->sourceAddress();
            auto asyncSink = std::make_unique<BMMQ::AsyncMidiSink>(std::move(alsaSink));
            machine.pluginManager().add(std::make_unique<BMMQ::PsgMidiPlugin>(std::move(asyncSink)));
            std::cout << "MIDI interface: " << sourceAddress;
            if (*options.midiOutputPort != "subscribers") {
                std::cout << " -> " << *options.midiOutputPort;
            }
            std::cout << '\n';
        }
#else
        if (options.midiOutputPort.has_value()) {
            throw std::runtime_error("Live MIDI output requires an ALSA-enabled build");
        }
#endif
        if (frontend != nullptr || audioTransport != nullptr ||
            options.midiOutputFilePath.has_value() || options.midiOutputPort.has_value()) {
            machine.pluginManager().initialize(machine.mutableView());
        }
        if (frontend != nullptr) {
            frontend->requestWindowVisibility(true);
            frontend->serviceFrontend();
        }

        std::unordered_set<std::string> loadedAudioProcessorIds;
        for (const auto& processorPath : options.audioProcessorPluginPaths) {
            auto module = BMMQ::Plugin::DynamicPluginModule::load(processorPath);
            const auto ids = module.audioProcessorIds();
            if (ids.empty()) {
                throw std::runtime_error("Audio processor module has no processors: " + processorPath.string());
            }
            for (const auto& id : ids) {
                if (!loadedAudioProcessorIds.emplace(id).second) {
                    throw std::runtime_error("Duplicate audio processor id: " + id);
                }
                std::string configJson;
                const auto config = std::find_if(options.audioProcessorConfigs.begin(),
                    options.audioProcessorConfigs.end(), [&id](const auto& item) { return item.pluginId == id; });
                if (config != options.audioProcessorConfigs.end()) {
                    std::ifstream input(config->jsonPath, std::ios::binary);
                    if (!input) throw std::runtime_error("Unable to read audio processor config: " + config->jsonPath.string());
                    configJson.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
                }
                const auto& engineConfig = machine.audioService().engine().config();
                auto processor = module.createAudioProcessor(
                    id, static_cast<std::uint32_t>(engineConfig.sourceSampleRate),
                    engineConfig.channelCount, engineConfig.frameChunkSamples, std::move(configJson));
                if (!machine.audioService().addProcessor(std::move(processor))) {
                    throw std::runtime_error("Unable to attach audio processor: " + id);
                }
                std::cout << "Audio processor: " << id << " (" << processorPath << ")\n";
            }
        }

        std::unique_ptr<BMMQ::IAudioOutputBackend> audioOutput;
        if (options.audioEnabled) {
            try {
                if (options.audioBackend == "sdl") {
                    const auto executablePath = (argc > 0 && argv != nullptr)
                        ? std::filesystem::path(argv[0]) : std::filesystem::path("timeEmulator");
                    const auto audioPluginPath = options.audioPluginPath.value_or(
                        BMMQ::defaultAudioOutputPluginPath(executablePath));
                    audioOutput = BMMQ::loadAudioOutputPlugin(audioPluginPath, "sdl");
                    std::cout << "Audio module: " << audioPluginPath << '\n';
                } else if (options.audioBackend == "dummy") {
                    audioOutput = std::make_unique<BMMQ::DummyAudioOutputBackend>();
                } else if (options.audioBackend == "file") {
                    audioOutput = std::make_unique<BMMQ::FileAudioOutputBackend>();
                } else {
                    throw std::invalid_argument("Unknown audio backend: " + options.audioBackend);
                }
                auto& audioService = machine.audioService();
                const int channels = std::max<int>(audioService.engine().config().channelCount, 1);
                const bool opened = audioOutput->open(audioService.engine(), {
                    .backend = options.audioBackend,
                    .requestedSampleRate = audioService.engine().config().sourceSampleRate,
                    .callbackChunkSamples = static_cast<std::size_t>(256 * channels),
                    .readyQueueChunks = static_cast<std::size_t>(
                        std::clamp<std::uint32_t>(options.audioReadyQueueChunks, 1u, 64u)),
                    .channels = channels,
                    .filePath = options.audioOutputFilePath.value_or(std::filesystem::path{}),
                    .audioService = &audioService,
                });
                if (!opened) {
                    std::cerr << "warning: audio output failed: " << audioOutput->lastError()
                              << "; continuing without device audio\n";
                    audioOutput.reset();
                }
            } catch (const std::exception& ex) {
                std::cerr << "warning: " << ex.what() << "; continuing without device audio\n";
                audioOutput.reset();
            }
        }

        std::cout << "Core: " << descriptor.id << '\n';
        std::cout << "Executor policy: " << activeExecutorPolicy.metadata().id << '\n';
        std::cout << "Execution backend: "
                  << BMMQ::executionBackendName(activeExecutorPolicy.backend()) << '\n';
        std::cout << "Loaded ROM: " << options.romPath << " ("
            << romSize << " bytes)\n";
        if (options.bootRomPath.has_value()) {
            std::cout << "Loaded boot ROM: " << *options.bootRomPath << '\n';
        }
        for (const auto& visualPackPath : options.visualPackPaths) {
            std::cout << "Loaded visual pack: " << visualPackPath << '\n';
        }
        if (options.visualPackReload) {
            std::cout << "Visual pack reload polling enabled\n";
        }
        if (options.visualCapturePath.has_value()) {
            std::cout << "Capturing visual resources to: " << *options.visualCapturePath << '\n';
        }
        if (frontend != nullptr) {
            std::cout << "Frontend: " << frontend->backendStatusSummary() << '\n';
        }
        if (audioOutput != nullptr) {
            std::cout << "Audio output: " << audioOutput->name() << '\n';
        }
        if (options.timingProfile.has_value()) {
            std::cout << "Timing profile: " << *options.timingProfile << '\n';
        }
        if (options.stepLimit.has_value()) {
            std::cout << "Running for " << *options.stepLimit << " instruction steps\n";
        } else {
            std::cout << "Running until the window is closed or Ctrl+C is pressed\n";
        }

        std::uint64_t steps = 0;
        std::uint64_t emulatedCycles = 0;
        const auto cpuClockHz = machine.clockHz();
        using SteadyClock = std::chrono::steady_clock;
        constexpr auto kFrontendServicePeriod = std::chrono::milliseconds(1);
        constexpr auto kMaxCatchUpWindow = std::chrono::milliseconds(8);
        constexpr auto kMinSleepQuantum = std::chrono::milliseconds(1);
        constexpr std::uint32_t kMaxFrontendServiceTicksPerWake = 2u;
        const double kMinInstructionCycles = 4.0;
        const double kExecutionSliceSeconds = 0.001;
        const double kFrontendServiceSliceSeconds = 0.001;

        BMMQ::TimingService timingService;
        BMMQ::TimingConfig timingConfig;
        timingConfig.baseClockHz = static_cast<double>(cpuClockHz);
        const auto timingProfile = options.timingProfile.has_value()
            ? BMMQ::parseTimingPolicyProfile(*options.timingProfile)
            : BMMQ::TimingPolicyProfile::Balanced;
        BMMQ::applyTimingPolicyProfileDefaults(timingProfile, timingConfig);
        timingConfig.speedMultiplier = options.speedMultiplier;
        timingConfig.minInstructionCycles = kMinInstructionCycles;
        timingConfig.executionSliceSeconds = kExecutionSliceSeconds;
        timingConfig.frontendServiceSliceSeconds = kFrontendServiceSliceSeconds;
        timingConfig.maxCatchUp = kMaxCatchUpWindow;
        if (!options.timingProfile.has_value()) {
            timingConfig.minSleepQuantum = kMinSleepQuantum;
        }
        timingConfig.maxCyclesPerWake = static_cast<double>(cpuClockHz) * 0.004;
        timingConfig.throttled = !options.unthrottled;
        timingService.configure(timingConfig);
        BMMQ::TimingEngine timingEngine(timingConfig);

        auto initialNow = SteadyClock::now();
        const auto runStartedAt = initialNow;
        auto nextFrontendService = initialNow + kFrontendServicePeriod;
        timingService.start(initialNow);
        timingEngine.start(initialNow);
        if (options.startPaused) {
            timingService.setPaused(true);
        }

        VisualReloadPollState visualReloadPollState;
        constexpr auto kVisualReloadPollInterval = std::chrono::milliseconds(125);

        auto refreshVisualReloadWatchList = [&]() {
            if (!options.visualPackReload) {
                return;
            }
            auto watchedPaths = machine.visualOverrideService().watchedReloadPaths();
            if (watchedPaths.empty()) {
                watchedPaths = options.visualPackPaths;
            }

            std::lock_guard<std::mutex> lock(visualReloadPollState.mutex);
            visualReloadPollState.watchedPaths = std::move(watchedPaths);

            std::map<std::string, std::filesystem::file_time_type> refreshed;
            for (const auto& watchedPath : visualReloadPollState.watchedPaths) {
                const auto key = watchedPath.lexically_normal().string();
                refreshed[key] = fileWriteTime(watchedPath);
            }
            visualReloadPollState.lastWriteTimes = std::move(refreshed);
        };

        auto scheduleVisualReloadProbe = [&](SteadyClock::time_point now) {
            if (!options.visualPackReload || now < visualReloadPollState.nextPollDue) {
                return;
            }
            visualReloadPollState.nextPollDue = now + kVisualReloadPollInterval;

            if (visualReloadPollState.pollInFlight.exchange(true, std::memory_order_acq_rel)) {
                return;
            }

            machine.visualOverrideService().recordAsyncProbeSubmission();
            const bool queued = backgroundTaskService.submit(BMMQ::BackgroundJobCategory::VisualReload,
                [&visualReloadPollState, &machine]() {
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(visualReloadPollState.mutex);
                    for (const auto& watchedPath : visualReloadPollState.watchedPaths) {
                        const auto key = watchedPath.lexically_normal().string();
                        const auto currentWriteTime = fileWriteTime(watchedPath);
                        const auto it = visualReloadPollState.lastWriteTimes.find(key);
                        if (it == visualReloadPollState.lastWriteTimes.end()) {
                            visualReloadPollState.lastWriteTimes.emplace(key, currentWriteTime);
                            continue;
                        }
                        if (it->second != currentWriteTime) {
                            it->second = currentWriteTime;
                            changed = true;
                        }
                    }
                }

                if (changed) {
                    visualReloadPollState.reloadRequested.store(true, std::memory_order_release);
                }
                visualReloadPollState.pollInFlight.store(false, std::memory_order_release);
                });

            if (!queued) {
                visualReloadPollState.pollInFlight.store(false, std::memory_order_release);
            }
        };

        refreshVisualReloadWatchList();

        std::ofstream diagnosticsReport;
        const auto diagnosticsIntervalMs = std::max<std::uint32_t>(1u, options.diagnosticsIntervalMs);
        const auto diagnosticsInterval = std::chrono::milliseconds(diagnosticsIntervalMs);
        auto nextDiagnosticsReport = initialNow + diagnosticsInterval;
        if (options.diagnosticsReportPath.has_value()) {
            const auto& diagnosticsPath = *options.diagnosticsReportPath;
            if (diagnosticsPath.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(diagnosticsPath.parent_path(), ec);
            }
            diagnosticsReport.open(diagnosticsPath, std::ios::out | std::ios::trunc);
            if (!diagnosticsReport) {
                throw std::runtime_error("Unable to open diagnostics report file: " + diagnosticsPath.string());
            }
            std::cout << "Diagnostics report: " << diagnosticsPath
                      << " (interval=" << diagnosticsIntervalMs << "ms)\n";
        }

        auto emitDiagnostics = [&](SteadyClock::time_point now, bool force) {
            if (!diagnosticsReport.is_open()) {
                return;
            }
            if (!force && now < nextDiagnosticsReport) {
                return;
            }
            if (!force) {
                while (nextDiagnosticsReport <= now) {
                    nextDiagnosticsReport += diagnosticsInterval;
                }
            }

            const auto timingStats = timingService.stats();
            std::optional<BMMQ::FrontendStats> frontendStats;
            if (frontend != nullptr) {
                frontendStats = frontend->stats();
            }
            if (audioTransport != nullptr) {
                if (!frontendStats.has_value()) frontendStats.emplace();
                populateAudioDiagnostics(*frontendStats, audioTransport->stats(),
                                         machine.audioService(), audioOutput.get());
            }
            std::optional<GameBoyMachine::BlockCacheStats> blockCacheStats;
            std::optional<std::string> stateFingerprint;
            if (auto* gameBoyMachine = dynamic_cast<GameBoyMachine*>(&machine);
                gameBoyMachine != nullptr) {
                blockCacheStats = gameBoyMachine->blockCacheStats();
                // Full guest state hashing is intentionally a terminal-sample
                // operation so periodic observability does not perturb hot-path
                // performance measurements.
                if (force) {
                    stateFingerprint = gameBoyMachine->deterministicStateFingerprint();
                }
            }

            writeDiagnosticsSample(diagnosticsReport,
                                   runStartedAt,
                                   now,
                                   emulatedCycles,
                                   steps,
                                   cpuClockHz,
                                   frontendStats.has_value() ? &*frontendStats : nullptr,
                                   timingStats,
                                   backgroundTaskService.stats(),
                                   blockCacheStats.has_value() ? &*blockCacheStats : nullptr,
                                   options.cpuDetailedTiming,
                                   stateFingerprint,
                                   activeExecutorPolicy.metadata().id,
                                   activeExecutorPolicy.backend());
            diagnosticsReport.flush();
        };

        auto pollVisualPackReload = [&]() {
            if (machine.visualOverrideService().pollBackgroundWork()) {
                machine.visualOverrideService().recordAsyncProbeReloadApplied();
                refreshVisualReloadWatchList();
            }
            if (const auto warning = machine.visualOverrideService().takeReloadWarning(); warning.has_value()) {
                std::cerr << "warning: " << *warning << '\n';
            }
            if (!options.visualPackReload) {
                return;
            }
            scheduleVisualReloadProbe(SteadyClock::now());
            if (!visualReloadPollState.reloadRequested.exchange(false, std::memory_order_acq_rel)) {
                return;
            }
            machine.visualOverrideService().recordAsyncProbeChangeDetected();

            if (!machine.visualOverrideService().requestReloadChangedPacks()) {
                visualReloadPollState.reloadRequested.store(true, std::memory_order_release);
            }
        };

        auto serviceFrontend = [&]() -> bool {
            if (audioOutput != nullptr) audioOutput->service();
            if (frontend == nullptr) {
                return false;
            }
            frontend->serviceFrontend();
            return frontend->quitRequested();
        };

        std::atomic<bool> stopRequested{false};
        std::atomic<bool> emulationFinished{false};
        std::atomic<bool> frontendInputTickPending{false};
        std::exception_ptr emulationFailure;

        auto serviceFrontendUntil = [&](SteadyClock::time_point now) -> bool {
            if ((frontend == nullptr && audioOutput == nullptr) || now < nextFrontendService) {
                return false;
            }

            const auto lateness = now - nextFrontendService;
            std::uint32_t scheduledTicks = 1u;
            if (lateness > kFrontendServicePeriod) {
                const auto behindPeriods =
                    static_cast<std::uint32_t>(lateness / kFrontendServicePeriod);
                scheduledTicks += behindPeriods;
            }
            std::uint32_t executedTicks = 0u;
            const auto ticksToRun = std::min<std::uint32_t>(scheduledTicks, kMaxFrontendServiceTicksPerWake);
            const auto mergedTicks = scheduledTicks - ticksToRun;

            for (std::uint32_t tick = 0u; tick < ticksToRun; ++tick) {
                ++executedTicks;
                if (serviceFrontend()) {
                    timingService.noteFrontendServiceTick(scheduledTicks, executedTicks, lateness);
                    return true;
                }
                nextFrontendService += kFrontendServicePeriod;
            }
            if (mergedTicks != 0u) {
                nextFrontendService += kFrontendServicePeriod * mergedTicks;
            }
            if (now - nextFrontendService > kMaxCatchUpWindow) {
                nextFrontendService = now + kFrontendServicePeriod;
            }
            timingService.noteFrontendServiceTick(scheduledTicks, executedTicks, lateness);
            frontendInputTickPending.store(true, std::memory_order_release);
            return false;
        };

        auto runEmulationLane = [&]() {
            try {
                class TimingRetirementSink final : public BMMQ::InstructionRetirementSink {
                public:
                    TimingRetirementSink(BMMQ::TimingEngine& engine,
                                         const BMMQ::TimingConfig& config,
                                         std::atomic<bool>& stopRequested,
                                         std::uint64_t& steps,
                                         std::uint64_t& emulatedCycles,
                                         double& wakeCycles,
                                         bool& timingSliceComplete,
                                         double minInstructionCycles,
                                         const std::optional<std::uint64_t>& stepLimit)
                        : engine_(engine), config_(config), stopRequested_(stopRequested),
                          steps_(steps), emulatedCycles_(emulatedCycles),
                          wakeCycles_(wakeCycles), timingSliceComplete_(timingSliceComplete),
                          minInstructionCycles_(minInstructionCycles),
                          stepLimit_(stepLimit) {}

                    BMMQ::InstructionRetirementDecision retireInstruction(
                        const BMMQ::CpuFeedback& feedback,
                        const BMMQ::ExecutionSliceProgress&) override
                    {
                        ++steps_;
                        emulatedCycles_ += feedback.retiredCycles;
                        const auto retiredCycles = static_cast<double>(feedback.retiredCycles);
                        const auto chargedCycles =
                            std::max(minInstructionCycles_, retiredCycles);
                        wakeCycles_ += chargedCycles;
                        engine_.charge(retiredCycles);
                        const auto decision = engine_.recordExecutionSliceCycles(chargedCycles);
                        timingSliceComplete_ = decision.executionSliceComplete;
                        const bool stepLimitReached =
                            stepLimit_.has_value() && steps_ >= *stepLimit_;
                        if (decision.executionSliceComplete || !engine_.canExecute() ||
                            wakeCycles_ >= config_.maxCyclesPerWake || stepLimitReached ||
                            stopRequested_.load(std::memory_order_acquire) ||
                            gStopRequested != 0) {
                            return BMMQ::InstructionRetirementDecision::exitSlice();
                        }
                        return BMMQ::InstructionRetirementDecision::continueSlice();
                    }

                private:
                    BMMQ::TimingEngine& engine_;
                    const BMMQ::TimingConfig& config_;
                    std::atomic<bool>& stopRequested_;
                    std::uint64_t& steps_;
                    std::uint64_t& emulatedCycles_;
                    double& wakeCycles_;
                    bool& timingSliceComplete_;
                    double minInstructionCycles_;
                    const std::optional<std::uint64_t>& stepLimit_;
                };

                while (!stopRequested.load(std::memory_order_acquire) && gStopRequested == 0) {
                    if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                        break;
                    }

                    if (frontend != nullptr && frontend->takeSaveStateRequest()) {
                        constexpr auto quickSavePath = "quicksave.ptstate";
                        try {
                            machine.save_state(quickSavePath);
                            std::cout << "Saved state: " << quickSavePath << '\n';
                        } catch (const std::exception& error) {
                            std::cerr << "warning: failed to save state: " << error.what() << '\n';
                        }
                    }

                    const auto now = SteadyClock::now();
                    if (frontendInputTickPending.exchange(false, std::memory_order_acq_rel)) {
                        machine.serviceInput();
                    }

                    timingEngine.applyControl(timingService.takeControlSnapshot());
                    timingEngine.update(now);

                    bool executedInstruction = false;
                    bool executionSliceActive = false;
                    std::uint32_t wakeExecutionSlices = 0u;
                    double wakeExecutionCycles = 0.0;
                    while (timingEngine.canExecute() &&
                           !stopRequested.load(std::memory_order_acquire) &&
                           gStopRequested == 0) {
                        if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                            break;
                        }
                        if (wakeExecutionSlices >= timingConfig.maxExecutionSlicesPerWake) {
                            timingService.noteWakeBurstSliceLimitHit();
                            break;
                        }
                        if (wakeExecutionCycles >= timingConfig.maxCyclesPerWake) {
                            timingService.noteWakeBurstCycleLimitHit();
                            break;
                        }

                        if (!executionSliceActive) {
                            timingEngine.beginExecutionSlice();
                            executionSliceActive = true;
                            ++wakeExecutionSlices;
                        }

                        const auto remainingSteps = options.stepLimit.has_value()
                            ? *options.stepLimit - steps
                            : std::uint64_t{256u};
                        const auto instructionBudget =
                            std::min<std::uint64_t>(remainingSteps, 256u);
                        const auto remainingWakeCycles = std::max(
                            1.0, timingConfig.maxCyclesPerWake - wakeExecutionCycles);
                        bool timingSliceComplete = false;
                        TimingRetirementSink retirementSink(
                            timingEngine, timingConfig, stopRequested, steps,
                            emulatedCycles, wakeExecutionCycles,
                            timingSliceComplete,
                            kMinInstructionCycles, options.stepLimit);
                        const auto sliceResult = machine.runSlice(
                            BMMQ::ExecutionBudget{
                                .maxInstructions = instructionBudget,
                                .maxCycles = static_cast<std::uint64_t>(remainingWakeCycles),
                                .stopOnSegmentBoundary = false,
                            },
                            &retirementSink);
                        if (sliceResult.progress.retiredInstructions == 0u) {
                            break;
                        }
                        executedInstruction = true;
                        if (timingSliceComplete) {
                            break;
                        }
                    }
                    timingService.recordWakeBurst(wakeExecutionCycles, wakeExecutionSlices);
                    timingService.publishEngineStats(timingEngine.stats());
                    emitDiagnostics(SteadyClock::now(), false);

                    if (stopRequested.load(std::memory_order_acquire) || gStopRequested != 0) {
                        break;
                    }

                    const auto idleNow = SteadyClock::now();
                    pollVisualPackReload();

                    if (!executedInstruction) {
                        const auto nextStepTime = timingEngine.nextWakeTime(idleNow);
                        const bool timingSleepDue =
                            timingEngine.shouldSleep(idleNow) && (nextStepTime > idleNow);

                        if (timingSleepDue && nextStepTime > idleNow) {
                            const auto requestedSleep =
                                std::chrono::duration_cast<std::chrono::nanoseconds>(nextStepTime - idleNow);
                            const auto beforeSleep = SteadyClock::now();
                            if (timingConfig.adaptiveSleepEnabled &&
                                requestedSleep > timingConfig.sleepSpinWindow &&
                                timingConfig.sleepSpinWindow > std::chrono::nanoseconds::zero()) {
                                const auto coarseWake = nextStepTime - timingConfig.sleepSpinWindow;
                                std::this_thread::sleep_until(coarseWake);
                                const auto spinStart = SteadyClock::now();
                                while (SteadyClock::now() < nextStepTime) {
                                    if (SteadyClock::now() - spinStart >= timingConfig.sleepSpinCap) {
                                        break;
                                    }
                                    std::this_thread::yield();
                                }
                            } else {
                                std::this_thread::sleep_until(nextStepTime);
                            }
                            const auto afterSleep = SteadyClock::now();
                            const auto actualSleep =
                                std::chrono::duration_cast<std::chrono::nanoseconds>(afterSleep - beforeSleep);
                            timingService.noteHostSleep(requestedSleep, actualSleep);
                        }
                    }
                }
            } catch (...) {
                emulationFailure = std::current_exception();
                stopRequested.store(true, std::memory_order_release);
            }
            emulationFinished.store(true, std::memory_order_release);
        };

        std::thread emulationThread(runEmulationLane);

        // Window/event frontends require host-thread affinity; this is the UI/render lane.
        try {
            while (!emulationFinished.load(std::memory_order_acquire)) {
                const auto now = SteadyClock::now();
                if (gStopRequested != 0 || serviceFrontendUntil(now)) {
                    stopRequested.store(true, std::memory_order_release);
                    break;
                }
                if (frontend == nullptr && audioOutput == nullptr) {
                    std::this_thread::sleep_for(kFrontendServicePeriod);
                } else if (nextFrontendService > now) {
                    std::this_thread::sleep_until(nextFrontendService);
                } else {
                    std::this_thread::yield();
                }
            }
        } catch (...) {
            stopRequested.store(true, std::memory_order_release);
            emulationThread.join();
            throw;
        }

        stopRequested.store(true, std::memory_order_release);
        emulationThread.join();
        if (emulationFailure != nullptr) {
            std::rethrow_exception(emulationFailure);
        }

        serviceFrontend();
        if (machine.visualOverrideService().capturing()) {
            machine.visualOverrideService().endCapture();
        }
        emitDiagnostics(SteadyClock::now(), true);
        if (audioOutput != nullptr) audioOutput->close();
        if (!options.visualPackPaths.empty() || options.visualCapturePath.has_value()) {
            (void)machine.visualOverrideService().captureStats();
            std::cout << machine.visualOverrideService().authorDiagnosticsReport();
        }

        std::cout << "Stopped after " << steps << " instruction steps";
        const auto stopSummary = machine.stopSummary();
        if (!stopSummary.empty()) {
            std::cout << ":\n" << stopSummary << '\n';
        } else {
            std::cout << '\n';
        }
        // Detach the frontend on its owning host thread before service destruction.
        (void)backgroundTaskService.waitUntilIdle(std::chrono::seconds(10));
        machine.pluginManager().shutdown(machine.mutableView());
        machine.flushPendingBackgroundWork();
        backgroundTaskService.shutdown();
        return EXIT_SUCCESS;
    } catch (const std::invalid_argument& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        printUsage((argc > 0 && argv != nullptr) ? argv[0] : "timeEmulator");
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
