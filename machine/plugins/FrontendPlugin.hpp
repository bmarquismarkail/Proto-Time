#ifndef BMMQ_FRONTEND_PLUGIN_HPP
#define BMMQ_FRONTEND_PLUGIN_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../InputTypes.hpp"
#include "../DebugSnapshotTypes.hpp"
#include "../MachineLifecycleCoordinator.hpp"
#include "input/InputPlugin.hpp"
#include "IoPlugin.hpp"
#include "video/VideoFrame.hpp"
#include "video/VideoPlugin.hpp"

namespace BMMQ {

class DebugSnapshotService;

struct FrontendConfig {
    std::string windowTitle = "T.I.M.E. Frontend";
    std::uint32_t windowScale = 2;
    // HD texture replacement scale factor. When > 1, output frames are scaled
    // by this factor and replaced tiles sample their replacement images at the
    // higher resolution. Default 1 preserves existing behavior. Clamped to [1, 8].
    std::uint32_t hdScale = 1;
    int frameWidth = 160;
    int frameHeight = 144;
    bool enableVideo = true;
    bool enableInput = true;
    VideoPresenterMode videoPresenterMode = VideoPresenterMode::Auto;
    bool autoInitializeBackend = false;
    bool enableRenderServiceThread = false;
    bool createHiddenWindowOnInitialize = false;
    bool pumpBackendEventsOnInputSample = true;
    bool autoPresentOnVideoEvent = true;
    bool showWindowOnPresent = false;
    // Debug/test inspection only. Full-frame retention is kept off the normal
    // render path unless a consumer explicitly requests it.
    bool retainLastPresentedFrame = false;
    bool retainDebugSnapshots = false;
    VideoPresenterPolicy videoPresenterPolicy = VideoPresenterPolicy::HardwarePreferredWithFallback;
};

enum class RenderServiceState : uint8_t {
    Stopped = 0,
    Starting,
    Active,
    Stopping,
    Faulted,
};

enum class LifecycleRecoveryTarget : uint8_t {
    None = 0,
    Video,
    Audio,
    VideoAndAudio,
};

struct FrontendStats {
    std::size_t attachCount = 0;
    std::size_t detachCount = 0;
    std::size_t videoEvents = 0;
    std::size_t audioEvents = 0;
    std::size_t inputEvents = 0;
    std::size_t inputPolls = 0;
    std::size_t inputSamplesProvided = 0;
    std::size_t framesPrepared = 0;
    std::size_t framesPresented = 0;
    std::size_t renderAttempts = 0;
    std::size_t videoFramesPublished = 0;
    std::size_t videoPresentCount = 0;
    std::size_t videoPresentFallbackCount = 0;
    std::size_t videoMailboxDepth = 0;
    std::size_t videoMailboxHighWaterFrames = 0;
    std::size_t videoMailboxStaleDropCount = 0;
    std::size_t videoMailboxStaleDebugDropCount = 0;
    std::size_t videoMailboxStaleRealtimeDropCount = 0;
    std::size_t videoMailboxOverwriteCount = 0;
    std::size_t videoMailboxOverwriteDebugCount = 0;
    std::size_t videoMailboxOverwriteRealtimeCount = 0;
    std::uint64_t videoLastPublishedGeneration = 0;
    std::uint64_t videoLastPresentedGeneration = 0;
    VideoPresenterMode configuredPresenterMode = VideoPresenterMode::Auto;
    VideoPresenterPolicy configuredPresenterPolicy = VideoPresenterPolicy::HardwarePreferredWithFallback;
    VideoPresenterMode activePresenterMode = VideoPresenterMode::Auto;
    bool videoPresenterUsedSoftwareFallback = false;
    std::size_t videoPresenterSoftwareFallbackCount = 0;
    VideoPresenterFallbackReason videoPresenterLastFallbackReason = VideoPresenterFallbackReason::None;
    std::size_t videoPresenterTextureRecreateCount = 0;
    std::size_t videoPresenterTextureUploadCount = 0;
    std::size_t videoPresenterRenderCount = 0;
    std::string videoPresenterRendererName;
    std::string videoSimdBackendName;
    std::size_t videoPresenterDirectIndexedFrameCount = 0;
    std::size_t videoPresenterArgbFrameCount = 0;
    std::size_t videoPresenterTextureLockCount = 0;
    std::uint32_t videoPresenterRendererFlags = 0;
    bool videoPresenterRendererAccelerated = false;
    bool videoPresenterRenderTargetSupported = false;
    std::int64_t videoPresenterExpansionDurationLastNanos = 0;
    std::int64_t videoPresenterExpansionDurationHighWaterNanos = 0;
    std::int64_t videoPresenterUploadDurationLastNanos = 0;
    std::int64_t videoPresenterUploadDurationHighWaterNanos = 0;
    std::int64_t videoPresenterRenderSubmitDurationLastNanos = 0;
    std::int64_t videoPresenterRenderSubmitDurationHighWaterNanos = 0;
    std::int64_t videoPresenterTotalDurationLastNanos = 0;
    std::int64_t videoPresenterTotalDurationHighWaterNanos = 0;
    std::size_t videoPresenterStageDurationSampleCount = 0;
    std::size_t videoPublishedDebugFrameCount = 0;
    std::size_t videoPublishedRealtimeFrameCount = 0;
    std::size_t videoPublishedDebugPixelBytes = 0;
    std::size_t videoPublishedRealtimePixelBytes = 0;
    std::size_t videoPublishedPixelBytes = 0;
    std::size_t videoPresentFreshFrameCount = 0;
    std::size_t videoPresentFromDebugFrameCount = 0;
    std::size_t videoPresentFromRealtimeFrameCount = 0;
    std::size_t videoPresentFallbackBlankCount = 0;
    std::size_t videoPresentFallbackLastValidCount = 0;
    std::size_t videoPresentGenerationGap0 = 0;
    std::size_t videoPresentGenerationGap1 = 0;
    std::size_t videoPresentGenerationGap2To3 = 0;
    std::size_t videoPresentGenerationGap4Plus = 0;
    std::size_t videoStaleEpochDropCount = 0;
    std::size_t videoLifecycleEpochBumpCount = 0;
    std::uint64_t videoLifecycleEpoch = 1;
    std::uint64_t videoFrameAgeLastNs = 0;
    std::uint64_t videoFrameAgeHighWaterNs = 0;
    std::size_t videoFrameAgeUnder50usCount = 0;
    std::size_t videoFrameAge50To100usCount = 0;
    std::size_t videoFrameAge100To250usCount = 0;
    std::size_t videoFrameAge250To500usCount = 0;
    std::size_t videoFrameAge500usTo1msCount = 0;
    std::size_t videoFrameAge1To2msCount = 0;
    std::size_t videoFrameAge2To5msCount = 0;
    std::size_t videoFrameAge5To10msCount = 0;
    std::size_t videoFrameAgeOver10msCount = 0;
    std::int64_t videoPresenterPresentDurationLastNanos = 0;
    std::int64_t videoPresenterPresentDurationHighWaterNanos = 0;
    std::int64_t videoPresenterPresentDurationP50Nanos = 0;
    std::int64_t videoPresenterPresentDurationP95Nanos = 0;
    std::int64_t videoPresenterPresentDurationP99Nanos = 0;
    std::int64_t videoPresenterPresentDurationP999Nanos = 0;
    std::size_t videoPresenterPresentDurationSampleCount = 0;
    std::size_t videoPresenterPresentDurationUnder50usCount = 0;
    std::size_t videoPresenterPresentDuration50To100usCount = 0;
    std::size_t videoPresenterPresentDuration100To250usCount = 0;
    std::size_t videoPresenterPresentDuration250To500usCount = 0;
    std::size_t videoPresenterPresentDuration500usTo1msCount = 0;
    std::size_t videoPresenterPresentDuration1To2msCount = 0;
    std::size_t videoPresenterPresentDuration2To5msCount = 0;
    std::size_t videoPresenterPresentDuration5To10msCount = 0;
    std::size_t videoPresenterPresentDurationOver10msCount = 0;
    std::size_t videoRealtimePacketsAccepted = 0;
    std::size_t videoRealtimePacketsSkipped = 0;
    std::size_t videoRealtimePacketsBuiltOutsideLock = 0;
    std::size_t videoRealtimeRenderRequestCount = 0;
    std::size_t videoDebugRenderRequestCount = 0;
    std::size_t videoRealtimeRenderFromVBlankCount = 0;
    std::size_t videoRealtimeRenderFromScanlineCount = 0;
    std::size_t videoRealtimeRenderFromMemoryWriteCount = 0;
    std::size_t videoRealtimeRenderFromOtherCount = 0;
    std::size_t videoDebugRenderFromVBlankCount = 0;
    std::size_t videoDebugRenderFromScanlineCount = 0;
    std::size_t videoDebugRenderFromMemoryWriteCount = 0;
    std::size_t videoDebugRenderFromOtherCount = 0;
    std::size_t videoBuildDebugFrameCallCount = 0;
    std::uint64_t videoBuildDebugFrameTotalNs = 0;
    std::size_t videoBuildDebugFrameRealtimeReasonCount = 0;
    std::size_t videoBuildDebugFrameDebugReasonCount = 0;
    std::size_t videoBuildDebugFrameFallbackReasonCount = 0;
    std::size_t videoBuildDebugFrameUnknownReasonCount = 0;
    std::size_t videoBuildDebugFrameDebugConsumerActiveCount = 0;
    std::size_t videoBuildDebugFrameDebugConsumerInactiveCount = 0;
    std::size_t videoVisualOverrideLookupSampleCount = 0;
    std::uint64_t videoVisualOverrideLookupTotalNs = 0;
    std::uint64_t videoVisualOverrideLookupHighWaterNs = 0;
    std::size_t videoVisualOverrideApplySampleCount = 0;
    std::uint64_t videoVisualOverrideApplyTotalNs = 0;
    std::uint64_t videoVisualOverrideApplyHighWaterNs = 0;
    std::size_t videoDebugFrameBuildSkippedNoConsumerCount = 0;
    std::size_t videoDebugFrameBuildExecutedCount = 0;
    std::size_t videoVdpRenderBodySampleCount = 0;
    std::uint64_t videoVdpRenderBodyTotalNs = 0;
    std::uint64_t videoVdpRenderBodySetupNs = 0;
    std::uint64_t videoVdpRenderBodyBackgroundNs = 0;
    std::uint64_t videoVdpRenderBodyBackgroundSimpleNs = 0;
    std::uint64_t videoVdpRenderBodyBackgroundGeneralNs = 0;
    std::uint64_t videoVdpRenderBodyBackgroundTmsNs = 0;
    std::uint64_t videoVdpRenderBodySpriteProbeNs = 0;
    std::uint64_t videoVdpRenderBodySpriteOverlayNs = 0;
    std::uint64_t videoVdpRenderBodyOtherNs = 0;
    // --- Phase 81B: simple-background sub-path breakdown accumulators ---
    std::uint64_t videoVdpSimpleBitplaneDecodeNs = 0;
    std::uint64_t videoVdpSimplePaletteFbWriteNs = 0;
    std::uint64_t videoVdpSimpleLoopOtherNs = 0;
    std::uint64_t videoVdpMode4AttrTileCellsProcessed = 0;
    std::uint64_t videoVdpMode4AttrTileCellsFlipH = 0;
    std::uint64_t videoVdpMode4AttrTileCellsFlipV = 0;
    std::uint64_t videoVdpMode4AttrTileCellsPalette1 = 0;
    std::uint64_t videoVdpMode4AttrTileCellsPriority = 0;
    std::uint64_t videoVdpMode4AttrTileCellsFixedTopRows = 0;
    std::uint64_t videoVdpMode4AttrTileCellsFixedRightColumns = 0;
    std::uint64_t videoVdpMode4AttrTileCellsLeftBlankOrFineSkip = 0;
    std::uint64_t videoVdpMode4AttrTileCellsCommonCaseEligible = 0;
    std::uint64_t videoVdpMode4AttrCommonCaseEligiblePixelsWritten = 0;
    std::uint64_t videoVdpMode4SimplePathFrameCount = 0;
    std::uint64_t videoVdpMode4SimplePathRowsRendered = 0;
    std::uint64_t videoVdpMode4SimplePathPixelsWritten = 0;
    std::uint64_t videoVdpMode4SimplePathTileEntriesDecoded = 0;
    std::uint64_t videoVdpMode4SimplePathPatternRowsDecoded = 0;
    std::uint64_t videoVdpMode4SimplePathScrollXAlignedCount = 0;
    std::uint64_t videoVdpMode4SimplePathScrollYValueChanges = 0;
    std::uint64_t videoVdpMode4SimplePathUniqueTileRowsSeen = 0;
    std::uint64_t videoVdpMode4SimplePathUsedCount = 0;
    std::uint64_t videoVdpMode4GeneralPathUsedCount = 0;
    std::uint64_t videoTmsGraphicsPathUsedCount = 0;
    // T2.3 Phase 3: overall frame build duration histogram (separate from present)
    std::size_t videoFrameBuildDurationSampleCount = 0;
    std::int64_t videoFrameBuildDurationLastNanos = 0;
    std::int64_t videoFrameBuildDurationHighWaterNanos = 0;
    std::size_t videoFrameBuildDurationUnder50usCount = 0;
    std::size_t videoFrameBuildDuration50To100usCount = 0;
    std::size_t videoFrameBuildDuration100To250usCount = 0;
    std::size_t videoFrameBuildDuration250To500usCount = 0;
    std::size_t videoFrameBuildDuration500usTo1msCount = 0;
    std::size_t videoFrameBuildDuration1To2msCount = 0;
    std::size_t videoFrameBuildDuration2To5msCount = 0;
    std::size_t videoFrameBuildDuration5To10msCount = 0;
    std::size_t videoFrameBuildDurationOver10msCount = 0;
    /// Phase 38B: counts VBlank/scanline events where videoDebugFrameModel() was
    /// skipped because no debug consumer (DebugSnapshotService) was connected.
    /// In production mode this should equal videoEvents (all skipped).
    std::size_t videoDebugModelBuildSkipCount = 0;
    /// Phase 36A: counts VBlank/scanline frames for which the render-service
    /// notification (renderServiceFramePending_ + condvar) was sent after
    /// sharedStateMutex_ was released, reducing the VBlank critical section.
    std::size_t onVideoEventFrameNotifyOutsideLockCount = 0;
    std::size_t videoDebugSnapshotsBuilt = 0;
    std::size_t videoStateSnapshots = 0;
    std::uint64_t videoDebugSnapshotDurationLastNs = 0;
    std::uint64_t videoDebugSnapshotDurationHighWaterNs = 0;
    std::size_t audioRealtimePacketsAccepted = 0;
    std::size_t audioRealtimePacketsSkipped = 0;
    std::size_t audioBatchConfiguredChunks = 1;
    std::size_t audioBatchFlushCount = 0;
    std::size_t audioBatchFlushSamplesLast = 0;
    std::size_t audioBatchFlushSamplesMin = 0;
    std::size_t audioBatchFlushSamplesMax = 0;
    std::size_t audioBatchPacketsAccumulated = 0;
    std::size_t audioBatchPacketsFlushed = 0;
    std::size_t audioBatchFlushReasonTargetCount = 0;
    std::size_t audioBatchFlushReasonFormatChangeCount = 0;
    std::size_t audioBatchFlushReasonLifecycleCount = 0;
    std::size_t audioBatchCurrentSamples = 0;
    std::size_t audioStateSnapshotsBuilt = 0;
    std::uint64_t audioStateSnapshotDurationLastNs = 0;
    std::uint64_t audioStateSnapshotDurationHighWaterNs = 0;
    std::size_t audioPreviewsBuilt = 0;
    // Audio metrics are read from the shared AudioService/AudioEngine and are global
    // to the machine's audio pipeline rather than frontend-local.
    int audioSourceSampleRate = 48000;
    int audioDeviceSampleRate = 0;
    std::size_t audioCallbackChunkSamples = 0;
    std::size_t audioRingBufferCapacitySamples = 0;
    std::size_t audioBufferedHighWaterSamples = 0;
    std::size_t audioCallbackCount = 0;
    std::size_t audioSamplesDelivered = 0;
    std::size_t audioUnderrunCount = 0;
    std::size_t audioSilenceSamplesFilled = 0;
    std::size_t audioOverrunDropCount = 0;
    std::size_t audioDroppedSamples = 0;
    bool audioResamplingActive = false;
    double audioResampleRatio = 1.0;
    std::size_t audioSourceSamplesPushed = 0;
    std::size_t audioAppendCallCount = 0;
    std::size_t audioAppendSamplesRequested = 0;
    std::size_t audioAppendSamplesAccepted = 0;
    std::size_t audioAppendSamplesRejected = 0;
    std::size_t audioAppendSamplesTruncated = 0;
    std::size_t audioAppendBufferedSamplesLast = 0;
    std::size_t audioResampleSourceSamplesConsumed = 0;
    std::size_t audioResampleOutputSamplesProduced = 0;
    std::size_t audioPipelineCapacitySkipCount = 0;
    std::size_t audioReadyQueueDepth = 0;
    std::size_t audioTransportConfiguredReadyQueueChunks = 0;
    std::size_t audioTransportPrefillTargetChunks = 0;
    std::size_t audioTransportReadyQueueCapacityChunks = 0;
    std::size_t audioTransportReadyQueueUsableChunks = 0;
    std::size_t audioReadyQueueHighWaterChunks = 0;
    std::size_t audioReadyQueueLowWaterChunks = 0;
    std::size_t audioReadyQueueEmptyCount = 0;
    // T2.2 Phase 2: FIFO occupancy at drain callback tick
    std::size_t audioReadyQueueDrainOccupancyDepthLast = 0;
    std::size_t audioReadyQueueDrainOccupancyHighWaterChunks = 0;
    std::size_t audioReadyQueueDrainOccupancyLowWaterChunks = 0;
    std::size_t audioReadyQueueDrainOccupancySampleCount = 0;
    std::size_t audioReadyQueueDrainOccupancyUnder1ChunkCount = 0;
    std::size_t audioReadyQueueDrainOccupancy1ChunkCount = 0;
    std::size_t audioReadyQueueDrainOccupancy2ChunksCount = 0;
    std::size_t audioReadyQueueDrainOccupancy3ChunksCount = 0;
    std::size_t audioReadyQueueDrainOccupancy4ChunksCount = 0;
    std::size_t audioReadyQueueDrainOccupancy5PlusChunksCount = 0;
    std::size_t audioTransportDrainCallbackCount = 0;
    std::size_t audioTransportDrainRequestedSamples = 0;
    std::size_t audioTransportDrainReadySamples = 0;
    std::size_t audioTransportUnderrunCount = 0;
    std::size_t audioTransportSilenceSamplesFilled = 0;
    std::size_t audioTransportWorkerProductionAttempts = 0;
    std::size_t audioTransportWorkerProductionSourceEmptyFailures = 0;
    std::size_t audioTransportWorkerProductionReadyQueueFullFailures = 0;
    std::size_t audioTransportWorkerProducedBlocks = 0;
    std::size_t audioTransportWorkerLoopIterations = 0;
    std::size_t audioTransportWorkerWakeProducedBlocks0Count = 0;
    std::size_t audioTransportWorkerWakeProducedBlocks1Count = 0;
    std::size_t audioTransportWorkerWakeProducedBlocks2Count = 0;
    std::size_t audioTransportWorkerWakeProducedBlocks3PlusCount = 0;
    std::size_t audioTransportWorkerProductionSourceBufferedSamplesSuccessLast = 0;
    std::size_t audioTransportWorkerProductionSourceBufferedSamplesFailureLast = 0;
    std::size_t audioTransportWorkerWakePeriodMilliseconds = 0;
    std::size_t audioTransportDroppedReadyBlocks = 0;
    std::size_t audioTransportWorkerWakeCount = 0;
    std::size_t audioTransportWorkerCallbackWakeCount = 0;
    std::size_t audioTransportWorkerEmulationWakeCount = 0;
    std::size_t audioTransportWorkerTimeoutWakeCount = 0;
    std::size_t audioTransportWorkerWakeSourceBufferedSamplesLast = 0;
    std::size_t audioTransportWorkerWakeSourceBufferedSamplesHighWater = 0;
    std::size_t audioTransportWorkerWakeSourceBufferedSamplesLowWater = 0;
    std::size_t audioTransportAppendRecentPcmCallCount = 0;
    std::size_t audioTransportAppendRecentPcmSamplesAppended = 0;
    std::size_t audioTransportStaleEpochDropCount = 0;
    std::size_t audioTransportEpochBumpCount = 0;
    std::size_t audioTransportPrimedTransitionCount = 0;
    std::size_t audioTransportPrimingSilenceCallbackCount = 0;
    std::size_t audioTransportPrimingSilenceSamples = 0;
    std::uint64_t audioTransportLifecycleEpoch = 1;
    bool audioTransportPrimedForDrain = false;
    std::size_t audioTransportDrainDurationSampleCount = 0;
    std::int64_t audioTransportDrainDurationLastNanos = 0;
    std::int64_t audioTransportDrainDurationHighWaterNanos = 0;
    std::int64_t audioTransportDrainDurationP50Nanos = 0;
    std::int64_t audioTransportDrainDurationP95Nanos = 0;
    std::int64_t audioTransportDrainDurationP99Nanos = 0;
    std::int64_t audioTransportDrainDurationP999Nanos = 0;
    std::size_t audioTransportDrainDurationUnder50usCount = 0;
    std::size_t audioTransportDrainDuration50To100usCount = 0;
    std::size_t audioTransportDrainDuration100To250usCount = 0;
    std::size_t audioTransportDrainDuration250To500usCount = 0;
    std::size_t audioTransportDrainDuration500usTo1msCount = 0;
    std::size_t audioTransportDrainDuration1To2msCount = 0;
    std::size_t audioTransportDrainDuration2To5msCount = 0;
    std::size_t audioTransportDrainDuration5To10msCount = 0;
    std::size_t audioTransportDrainDurationOver10msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatencySampleCount = 0;
    std::int64_t audioTransportWorkerEmulationWakeLatencyLastNs = 0;
    std::int64_t audioTransportWorkerEmulationWakeLatencyHighWaterNs = 0;
    std::size_t audioTransportWorkerEmulationWakeLatencyUnder100usCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency100To500usCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency500usTo1msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency1To2msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency2To5msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency5To10msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatency10To20msCount = 0;
    std::size_t audioTransportWorkerEmulationWakeLatencyOver20msCount = 0;
    std::size_t audioRealtimePacketSamplesLast = 0;
    std::size_t audioRealtimePacketSamplesMin = 0;
    std::size_t audioRealtimePacketSamplesMax = 0;
    std::uint32_t audioRealtimePacketSampleRateLast = 0;
    std::uint8_t audioRealtimePacketChannelCountLast = 0;
    std::uint64_t audioRealtimePacketPsgChunksEmittedLast = 0;
    std::uint64_t audioRealtimePacketPsgSamplesGeneratedTotalLast = 0;
    std::uint32_t audioRealtimePacketPsgChunkSamplesLast = 0;
    std::uint32_t audioRealtimePacketPsgChunkSamplesMin = 0;
    std::uint32_t audioRealtimePacketPsgChunkSamplesMax = 0;
    std::uint32_t audioRealtimePacketPsgPendingSamplesLast = 0;
    std::size_t audioQueueWrites = 0;
    std::size_t audioSamplesQueued = 0;
    std::size_t audioQueueLowWaterHits = 0;
    std::size_t audioQueueHighWaterSkips = 0;
    std::size_t audioQueueRecoveryClears = 0;
    std::uint32_t lastQueuedAudioBytes = 0;
    std::uint32_t peakQueuedAudioBytes = 0;
    std::size_t backendInitAttempts = 0;
    std::size_t buttonTransitions = 0;
    std::size_t quitRequests = 0;
    std::size_t hostEventsHandled = 0;
    std::size_t keyEventsHandled = 0;
    std::size_t eventPumpCalls = 0;
    std::size_t backendEventsTranslated = 0;
    std::size_t serviceCalls = 0;
    double timingCycleDebt = 0.0;
    std::uint64_t timingWakeBurstSamples = 0;
    std::uint64_t timingWakeBurstSliceLimitHitCount = 0;
    std::uint64_t timingWakeBurstCycleLimitHitCount = 0;
    std::uint32_t timingWakeBurstSlicesLast = 0;
    std::uint32_t timingWakeBurstSlicesHighWater = 0;
    double timingWakeBurstCyclesLast = 0.0;
    double timingWakeBurstCyclesHighWater = 0.0;
    std::uint64_t timingSleepCalls = 0;
    std::uint64_t timingSleepWakeEarlyCount = 0;
    std::uint64_t timingSleepWakeLateCount = 0;
    std::uint64_t timingSleepWakeJitterUnder100usCount = 0;
    std::uint64_t timingSleepWakeJitter100To500usCount = 0;
    std::uint64_t timingSleepWakeJitter500usTo2msCount = 0;
    std::uint64_t timingSleepWakeJitterOver2msCount = 0;
    std::uint64_t timingSleepWakeLateStreakCurrent = 0;
    std::uint64_t timingSleepWakeLateStreakHighWater = 0;
    std::uint64_t timingSleepOvershootCount = 0;
    std::int64_t timingSleepOvershootHighWaterNanos = 0;
    std::int64_t timingSleepOvershootLastNanos = 0;
    std::uint64_t timingFrontendTicksScheduled = 0;
    std::uint64_t timingFrontendTicksExecuted = 0;
    std::uint64_t timingFrontendTicksMerged = 0;
    std::int64_t timingFrontendTickDelayLastNanos = 0;
    std::int64_t timingFrontendTickDelayHighWaterNanos = 0;
    std::string timingProfileName = "balanced";
    std::size_t renderServiceLoopCount = 0;
    std::size_t renderServicePresentAttempts = 0;
    std::size_t renderServicePresentSuccessCount = 0;
    std::size_t renderServicePresentFailureCount = 0;
    std::size_t renderServicePresentCallsOutsideLock = 0;
    std::size_t renderServiceLightweightSyncCount = 0;
    std::size_t renderServiceEventPumpCount = 0;
    std::size_t renderServiceSleepCount = 0;
    std::size_t renderServiceSleepOvershootCount = 0;
    std::size_t renderServiceFrameWakeCount = 0;
    std::size_t renderServiceTimeoutWakeCount = 0;
    std::size_t renderServiceDeferredPresentFastSleepCount = 0;
    RenderServiceState renderServiceState = RenderServiceState::Stopped;
    MachineTransitionOutcome lifecycleLastOutcome = MachineTransitionOutcome::Succeeded;
    MachineTransitionFailureStage lifecycleLastFailureStage = MachineTransitionFailureStage::None;
    std::size_t lifecycleLastRetryCountUsed = 0;
    bool lifecycleLastRejectedForReentry = false;
    bool lifecycleDegradedHeadlessVideoActive = false;
    bool lifecycleDegradedAudioDisabledActive = false;
    std::size_t lifecycleTransitionCount = 0;
    std::size_t lifecycleTransitionSuccessCount = 0;
    std::size_t lifecycleTransitionDegradedCount = 0;
    std::size_t lifecycleTransitionFailureCount = 0;
    std::size_t lifecycleTransitionReentryAttemptCount = 0;
    std::size_t lifecycleNestedTransitionRejectCount = 0;
    std::size_t lifecycleReasonRomLoadCount = 0;
    std::size_t lifecycleReasonHardResetCount = 0;
    std::size_t lifecycleReasonAudioBackendRestartCount = 0;
    std::size_t lifecycleReasonVideoBackendRestartCount = 0;
    std::size_t lifecycleReasonConfigReconfigureCount = 0;
    std::uint64_t lifecycleTransitionDurationP50Ns = 0;
    std::uint64_t lifecycleTransitionDurationP95Ns = 0;
    std::uint64_t lifecycleTransitionDurationMaxNs = 0;
    std::size_t lifecycleVideoPresenterEnsureCoordinatorCount = 0;
    std::size_t lifecycleVideoPresenterEnsureDirectCount = 0;
    std::size_t lifecycleRecoveryVideoAttemptCount = 0;
    std::size_t lifecycleRecoveryVideoSuccessCount = 0;
    std::size_t lifecycleRecoveryVideoFailureCount = 0;
    std::size_t lifecycleRecoveryAudioAttemptCount = 0;
    std::size_t lifecycleRecoveryAudioSuccessCount = 0;
    std::size_t lifecycleRecoveryAudioFailureCount = 0;
    std::size_t lifecycleRecoveryCooldownSuppressCount = 0;
    LifecycleRecoveryTarget lifecycleRecoveryLastTarget = LifecycleRecoveryTarget::None;
    MachineTransitionReason lifecycleRecoveryLastTransitionReason = MachineTransitionReason::ConfigReconfigure;
    MachineTransitionOutcome lifecycleRecoveryLastTransitionOutcome = MachineTransitionOutcome::Succeeded;
    MachineTransitionFailureStage lifecycleRecoveryLastTransitionFailureStage = MachineTransitionFailureStage::None;
    bool lifecycleRecoveryLastTransitionRejectedForReentry = false;
    bool lifecycleRecoveryLastUsedCoordinator = false;
};

enum class FrontendHostEventType : uint8_t {
    None = 0,
    KeyDown = 1,
    KeyUp = 2,
    Quit = 3,
};

enum class FrontendHostKey : uint8_t {
    Unknown = 0,
    Right,
    Left,
    Up,
    Down,
    Z,
    X,
    Backspace,
    Return,
    Pause,
    ThrottleToggle,
    SingleStep,
    SpeedUp,
    SpeedDown,
    SaveState,
};

struct FrontendHostEvent {
    FrontendHostEventType type = FrontendHostEventType::None;
    FrontendHostKey key = FrontendHostKey::Unknown;
    bool repeat = false;
};

struct FrontendFrameBuffer {
    int width = 160;
    int height = 144;
    std::uint64_t generation = 0;
    std::vector<uint32_t> pixels;

    [[nodiscard]] bool empty() const noexcept
    {
        return pixels.empty();
    }

    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        return pixels.size();
    }
};

class IFrontendPlugin : public IVideoPlugin,
                           public IDigitalInputPlugin,
                           public IDigitalInputSourcePlugin {
public:
    ~IFrontendPlugin() override = default;

    [[nodiscard]] virtual const FrontendConfig& config() const noexcept = 0;
    [[nodiscard]] virtual FrontendStats stats() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::string>& diagnostics() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<VideoDebugFrameModel>& lastVideoDebugModel() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<DigitalInputStateView>& lastInputState() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<FrontendFrameBuffer>& lastFrame() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastRenderSummary() const noexcept = 0;
    [[nodiscard]] virtual bool windowVisible() const noexcept = 0;
    [[nodiscard]] virtual bool windowVisibilityRequested() const noexcept = 0;
    virtual void requestWindowVisibility(bool visible) = 0;
    virtual bool serviceFrontend() = 0;
    virtual void setQueuedDigitalInputMask(uint32_t pressedMask) = 0;
    virtual void clearQueuedDigitalInputMask() = 0;
    [[nodiscard]] virtual std::optional<uint32_t> queuedDigitalInputMask() const noexcept = 0;
    virtual void pressButton(InputButton button) = 0;
    virtual void releaseButton(InputButton button) = 0;
    [[nodiscard]] virtual bool isButtonPressed(InputButton button) const noexcept = 0;
    virtual void clearQuitRequest() noexcept = 0;
    [[nodiscard]] virtual bool quitRequested() const noexcept = 0;
    [[nodiscard]] virtual bool takeSaveStateRequest() noexcept = 0;
    [[nodiscard]] virtual std::string_view lastHostEventSummary() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastBackendError() const noexcept = 0;
    [[nodiscard]] virtual std::string backendStatusSummary() const = 0;
    [[nodiscard]] virtual bool handleHostEvent(const FrontendHostEvent& event) = 0;
    [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;
    [[nodiscard]] virtual bool backendReady() const noexcept = 0;
    [[nodiscard]] virtual bool tryInitializeBackend() = 0;
    [[nodiscard]] virtual std::size_t pumpBackendEvents() = 0;

    /// Inject an optional DebugSnapshotService.  When set, the plugin routes
    /// video debug model captures through bounded service queues
    /// (mutex-protected in the current implementation) so the render thread can
    /// drain them from serviceFrontend() without mutating emulation-lane state.
    /// Pass nullptr to disable (falls back to the inline sharedStateMutex_ path).
    virtual void setDebugSnapshotService(DebugSnapshotService* service) noexcept = 0;
    [[nodiscard]] virtual DebugSnapshotService* debugSnapshotService() const noexcept = 0;
};

} // namespace BMMQ

#endif // BMMQ_FRONTEND_PLUGIN_HPP
