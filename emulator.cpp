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
#include <vector>

#include "emulator/AudioKpiStatus.hpp"
#include "emulator/EmulatorConfig.hpp"
#include "emulator/EmulatorHost.hpp"
#include "machine/BackgroundTaskService.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"
#include "machine/TimingService.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"

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
              << "  --plugin <path>    Optional SDL frontend shared object override\n"
              << "  --steps <count>    Stop after a fixed number of instruction steps\n"
              << "  --scale <n>        SDL window scale factor (default: 3)\n"
              << "  --unthrottled      Run unthrottled (no wall-clock pacing)\n"
              << "  --speed <mult>     Start with speed multiplier (e.g. 2.0)\n"
              << "  --pause            Start paused (use single-step to advance)\n"
              << "  --diagnostics-report <path>\n"
              << "                     Write periodic runtime diagnostics samples to <path>\n"
              << "  --diagnostics-interval-ms <n>\n"
              << "                     Diagnostics sample interval in milliseconds (default: 1000)\n"
              << "  --no-audio         Disable frontend audio output\n"
              << "  --audio-backend <name>\n"
              << "                     Frontend audio backend: sdl, dummy, or file (default: sdl)\n"
              << "  --audio-ready-queue-chunks <n>\n"
              << "  --audio-batch-chunks <n>\n"
              << "                     Audio output ready-queue chunk depth (1-64, default: 3)\n"
              << "  --visual-pack <path>\n"
              << "                     Load a visual override pack.json; repeat to load multiple packs\n"
              << "  --visual-capture <dir>\n"
              << "                     Capture observed decoded visual resources for pack authoring\n"
              << "  --visual-pack-reload\n"
              << "                     Poll visual pack manifests/assets and reload changed packs\n"
              << "  --headless         Run without the SDL frontend plugin\n"
              << "  -h, --help         Show this help text\n\n"
              << "Controls:\n"
              << "  Arrow keys = directions, Z = Button1, X = Button2,\n"
              << "  Backspace = Meta1, Enter = Meta2\n";
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

void writeDiagnosticsSample(std::ostream& output,
                            std::chrono::steady_clock::time_point startedAt,
                            std::chrono::steady_clock::time_point now,
                            std::uint64_t emulatedCycles,
                            std::uint32_t cpuClockHz,
                            const BMMQ::SdlFrontendStats* frontendStats,
                            const BMMQ::TimingStats& timingStats) noexcept
{
    using namespace std::chrono;
    const auto elapsedNs = duration_cast<nanoseconds>(now - startedAt).count();
    const auto elapsedSeconds = static_cast<double>(elapsedNs) / 1'000'000'000.0;
    const auto effectiveCyclesPerSecond =
        (elapsedSeconds > 0.0) ? (static_cast<double>(emulatedCycles) / elapsedSeconds) : 0.0;
    const auto effectiveSpeed =
        (cpuClockHz != 0u) ? (effectiveCyclesPerSecond / static_cast<double>(cpuClockHz)) : 0.0;

    const BMMQ::SdlFrontendStats defaults{};
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
    output << ",\"effective_emulation_speed\":" << effectiveSpeed;
    output << ",\"effective_cycles_per_second\":" << effectiveCyclesPerSecond;
    output << ",\"active_timing_profile\":\""
           << jsonEscape(BMMQ::timingPolicyProfileName(timingStats.activeProfile)) << "\"";

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
    output << "}";

    output << ",\"audio\":{";
    output << "\"primed_for_drain\":" << (stats.audioTransportPrimedForDrain ? "true" : "false");
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
    output << ",\"capacity_chunks\":" << stats.audioTransportReadyQueueCapacityChunks;
    output << ",\"usable_chunks\":" << stats.audioTransportReadyQueueUsableChunks;
    output << ",\"depth_last\":" << stats.audioReadyQueueDepth;
    output << ",\"depth_high_water\":" << stats.audioReadyQueueHighWaterChunks;
    output << ",\"depth_low_water\":" << stats.audioReadyQueueLowWaterChunks;
    output << ",\"empty_count\":" << stats.audioReadyQueueEmptyCount;
    output << ",\"drop_count\":" << stats.audioTransportDroppedReadyBlocks;
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

    output << ",\"timing\":{";
    output << "\"frontend_ticks_scheduled\":" << stats.timingFrontendTicksScheduled;
    output << ",\"frontend_ticks_executed\":" << stats.timingFrontendTicksExecuted;
    output << ",\"frontend_ticks_merged\":" << stats.timingFrontendTicksMerged;
    output << ",\"wake_jitter_under_100us\":" << stats.timingSleepWakeJitterUnder100usCount;
    output << ",\"wake_jitter_100_to_500us\":" << stats.timingSleepWakeJitter100To500usCount;
    output << ",\"wake_jitter_500us_to_2ms\":" << stats.timingSleepWakeJitter500usTo2msCount;
    output << ",\"wake_jitter_over_2ms\":" << stats.timingSleepWakeJitterOver2msCount;
    output << "}";
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

        BMMQ::BackgroundTaskService backgroundTaskService;
        backgroundTaskService.start();

        auto bootstrapped = BMMQ::bootstrapMachine(options);
        auto& machine = *bootstrapped.machine;
        const auto& descriptor = bootstrapped.descriptor;
        const auto romSize = bootstrapped.romSize;
        if (auto* gameBoyMachine = dynamic_cast<GameBoyMachine*>(bootstrapped.machine.get());
            gameBoyMachine != nullptr) {
            gameBoyMachine->setBackgroundTaskService(&backgroundTaskService);
        }

        BMMQ::ISdlFrontendPlugin* frontend = nullptr;
        std::unique_ptr<BMMQ::ISdlFrontendPlugin> frontendPlugin;
        if (!options.headless) {
            BMMQ::SdlFrontendConfig config;
            config.windowTitle = "Proto-Time - " + std::string(descriptor.displayName) +
                " - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1u);
            config.frameWidth = descriptor.defaultFrameWidth;
            config.frameHeight = descriptor.defaultFrameHeight;
            config.autoInitializeBackend = true;
            // The SDL presenter opens windows hidden; ensure frames are
            // presented automatically so the window appears during normal runs.
            config.createHiddenWindowOnInitialize = false;
            config.pumpBackendEventsOnInputSample = false;
            config.autoPresentOnVideoEvent = true;
            config.showWindowOnPresent = true;
            config.enableAudio = options.audioEnabled;
            config.audioBackend = options.audioBackend;
            config.audioReadyQueueChunks =
                static_cast<std::size_t>(std::clamp<std::uint32_t>(options.audioReadyQueueChunks, 1u, 64u));
            config.audioBatchChunks =
                static_cast<std::size_t>(std::clamp<std::uint32_t>(options.audioBatchChunks, 1u, 16u));

            const auto pluginPath = options.pluginPath.value_or(
                BMMQ::defaultSdlFrontendPluginPath((argc > 0 && argv != nullptr)
                    ? std::filesystem::path(argv[0])
                    : std::filesystem::path("timeEmulator")));
            try {
                frontendPlugin = BMMQ::loadSdlFrontendPlugin(pluginPath, config);
                frontend = frontendPlugin.get();
                machine.pluginManager().add(std::move(frontendPlugin));
                machine.pluginManager().initialize(machine.mutableView());
                frontend->requestWindowVisibility(true);
                frontend->serviceFrontend();
            } catch (const std::exception& ex) {
                std::cerr << "warning: " << ex.what() << "; continuing headless\n";
            }
        }

        std::cout << "Core: " << descriptor.id << '\n';
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
            const bool queued = backgroundTaskService.submit([&visualReloadPollState, &machine]() {
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
                    machine.visualOverrideService().recordAsyncProbeChangeDetected();
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
            std::optional<BMMQ::SdlFrontendStats> frontendStats;
            if (frontend != nullptr) {
                frontendStats = frontend->stats();
            }

            writeDiagnosticsSample(diagnosticsReport,
                                   runStartedAt,
                                   now,
                                   emulatedCycles,
                                   cpuClockHz,
                                   frontendStats.has_value() ? &*frontendStats : nullptr,
                                   timingStats);
            diagnosticsReport.flush();
        };

        auto pollVisualPackReload = [&]() {
            if (!options.visualPackReload) {
                return;
            }
            scheduleVisualReloadProbe(SteadyClock::now());
            if (!visualReloadPollState.reloadRequested.exchange(false, std::memory_order_acq_rel)) {
                return;
            }

            const bool reloaded = machine.visualOverrideService().reloadChangedPacks();
            if (reloaded) {
                machine.visualOverrideService().recordAsyncProbeReloadApplied();
            }
            refreshVisualReloadWatchList();
            if (!reloaded) {
                if (const auto warning = machine.visualOverrideService().takeReloadWarning(); warning.has_value()) {
                    std::cerr << "warning: " << *warning << '\n';
                }
            }
        };

        auto serviceFrontend = [&]() -> bool {
            if (frontend == nullptr) {
                return false;
            }
            frontend->serviceFrontend();
            pollVisualPackReload();
            return frontend->quitRequested();
        };

        auto serviceFrontendUntil = [&](SteadyClock::time_point now) -> bool {
            bool servicedFrontend = false;
            if (frontend == nullptr || now < nextFrontendService) {
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
                servicedFrontend = true;
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
            if (servicedFrontend) {
                timingEngine.applyControl(timingService.takeControlSnapshot());
                machine.serviceInput();
            }
            return false;
        };

        while (gStopRequested == 0) {
            if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                break;
            }

            const auto now = SteadyClock::now();
            if (serviceFrontendUntil(now)) {
                break;
            }

            timingEngine.applyControl(timingService.takeControlSnapshot());
            timingEngine.update(now);

            bool executedInstruction = false;
            bool executionSliceActive = false;
            std::uint32_t wakeExecutionSlices = 0u;
            double wakeExecutionCycles = 0.0;
            while (timingEngine.canExecute() && gStopRequested == 0) {
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

                machine.step();
                ++steps;
                executedInstruction = true;
                emulatedCycles += machine.runtimeContext().getLastFeedback().retiredCycles;

                const auto retiredCycles = static_cast<double>(machine.runtimeContext().getLastFeedback().retiredCycles);
                const auto chargedCycles = std::max(kMinInstructionCycles, retiredCycles);
                wakeExecutionCycles += chargedCycles;
                timingEngine.charge(retiredCycles);
                const auto sliceDecision = timingEngine.recordExecutionSliceCycles(chargedCycles);

                if (sliceDecision.frontendServiceDue && serviceFrontendUntil(SteadyClock::now())) {
                    gStopRequested = 1;
                    break;
                }
                if (sliceDecision.executionSliceComplete) {
                    break;
                }
            }
            timingService.recordWakeBurst(wakeExecutionCycles, wakeExecutionSlices);
            timingService.publishEngineStats(timingEngine.stats());
            emitDiagnostics(SteadyClock::now(), false);

            if (gStopRequested != 0) {
                break;
            }

            const auto idleNow = SteadyClock::now();
            if (serviceFrontendUntil(idleNow)) {
                break;
            }
            if (frontend == nullptr) {
                pollVisualPackReload();
            }

            if (!executedInstruction) {
                const auto nextStepTime = timingEngine.nextWakeTime(idleNow);
                const auto frontendWakeTime = (frontend != nullptr) ? nextFrontendService : idleNow;
                const auto nextWakeTime = (frontend != nullptr)
                    ? std::min(frontendWakeTime, nextStepTime)
                    : nextStepTime;

                const bool frontendSleepDue = (frontend != nullptr) && (frontendWakeTime > idleNow);
                const bool timingSleepDue = timingEngine.shouldSleep(idleNow) && (nextStepTime > idleNow);

                if (timingSleepDue || frontendSleepDue) {
                    if (nextWakeTime > idleNow) {
                        const auto requestedSleep = std::chrono::duration_cast<std::chrono::nanoseconds>(nextWakeTime - idleNow);
                        const auto beforeSleep = SteadyClock::now();
                        if (timingConfig.adaptiveSleepEnabled && requestedSleep > timingConfig.sleepSpinWindow &&
                            timingConfig.sleepSpinWindow > std::chrono::nanoseconds::zero()) {
                            const auto coarseWake = nextWakeTime - timingConfig.sleepSpinWindow;
                            std::this_thread::sleep_until(coarseWake);
                            const auto spinStart = SteadyClock::now();
                            while (SteadyClock::now() < nextWakeTime) {
                                if (SteadyClock::now() - spinStart >= timingConfig.sleepSpinCap) {
                                    break;
                                }
                                std::this_thread::yield();
                            }
                        } else {
                            std::this_thread::sleep_until(nextWakeTime);
                        }
                        const auto afterSleep = SteadyClock::now();
                        const auto actualSleep = std::chrono::duration_cast<std::chrono::nanoseconds>(afterSleep - beforeSleep);
                        timingService.noteHostSleep(requestedSleep, actualSleep);
                    }
                }
            }
        }

        serviceFrontend();
        emitDiagnostics(SteadyClock::now(), true);
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
