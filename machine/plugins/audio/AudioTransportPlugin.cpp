#include "AudioTransportPlugin.hpp"

#include "machine/AudioService.hpp"
#include "machine/Machine.hpp"

namespace BMMQ {

template<typename T>
void AudioTransportPlugin::atomicMax(std::atomic<T>& target, T value) noexcept
{
    auto current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
}

template<typename T>
void AudioTransportPlugin::atomicMinNonZero(std::atomic<T>& target, T value) noexcept
{
    auto current = target.load(std::memory_order_relaxed);
    while ((current == 0u || value < current) &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
}

void AudioTransportPlugin::onAttach(MutableMachineView& view)
{
    discardBatch();
    service_ = &view.audioService();
    batchEpoch_ = service_->lifecycleEpoch();
}

void AudioTransportPlugin::onDetach(MutableMachineView&)
{
    flushBatch();
    service_ = nullptr;
}

void AudioTransportPlugin::discardBatch() noexcept
{
    batchSamples_.clear();
    batchPackets_ = 0u;
    currentSamples_.store(0u, std::memory_order_relaxed);
}

void AudioTransportPlugin::onMachineEvent(const MachineEvent& event, const MachineView&)
{
    // Native ROM loads also announce a stream boundary to attached plugins.
    if (event.type == MachineEventType::RomLoaded) discardBatch();
}

void AudioTransportPlugin::onAudioEvent(const MachineEvent& event, const MachineView& view)
{
    if (service_ == nullptr || event.type != MachineEventType::AudioFrameReady) return;
    const auto epoch = service_->lifecycleEpoch();
    if (batchEpoch_ != epoch) {
        discardBatch();
        batchEpoch_ = epoch;
    }
    events_.fetch_add(1u, std::memory_order_relaxed);
    auto packet = view.realtimeAudioPacket();
    if (!packet.has_value() || packet->contractVersion != RealtimeAudioPacket::kContractVersion) {
        skipped_.fetch_add(1u, std::memory_order_relaxed);
        return;
    }
    accepted_.fetch_add(1u, std::memory_order_relaxed);
    packetSamplesLast_.store(packet->pcmSamples.size(), std::memory_order_relaxed);
    atomicMinNonZero(packetSamplesMin_, packet->pcmSamples.size());
    atomicMax(packetSamplesMax_, packet->pcmSamples.size());
    sampleRateLast_.store(packet->sampleRate, std::memory_order_relaxed);
    channelCountLast_.store(packet->channelCount, std::memory_order_relaxed);
    psgChunksEmittedLast_.store(packet->psgChunksEmitted, std::memory_order_relaxed);
    psgSamplesGeneratedTotalLast_.store(packet->psgSamplesGeneratedTotal,
                                        std::memory_order_relaxed);
    psgChunkSamplesLast_.store(packet->psgChunkSamplesLast, std::memory_order_relaxed);
    psgChunkSamplesMin_.store(packet->psgChunkSamplesMin, std::memory_order_relaxed);
    psgChunkSamplesMax_.store(packet->psgChunkSamplesMax, std::memory_order_relaxed);
    psgPendingSamplesLast_.store(packet->psgPendingSamples, std::memory_order_relaxed);
    packetsAccumulated_.fetch_add(1u, std::memory_order_relaxed);
    if (batchChunks_ == 1u) {
        const auto sampleCount = packet->pcmSamples.size();
        AudioSourceBlock block;
        block.contractVersion = packet->contractVersion;
        block.sampleRate = packet->sampleRate;
        block.channelCount = packet->channelCount;
        block.frameCounter = packet->frameCounter;
        block.firstSampleFrame = packet->firstSampleFrame;
        block.mixedSamples = std::move(packet->pcmSamples);
        block.voices = std::move(packet->voices);
        block.voiceStems = std::move(packet->voiceStems);
        block.events = std::move(packet->events);
        (void)service_->submitSourceBlock(std::move(block));
        flushCount_.fetch_add(1u, std::memory_order_relaxed);
        flushLast_.store(sampleCount, std::memory_order_relaxed);
        atomicMinNonZero(flushMin_, sampleCount);
        atomicMax(flushMax_, sampleCount);
        packetsFlushed_.fetch_add(1u, std::memory_order_relaxed);
        return;
    }
    // Rich PSG metadata is sample-aligned per packet, so batching is only used
    // for legacy PCM-only packets. Preserve metadata-bearing packets directly.
    if (!packet->voices.empty() || !packet->events.empty()) {
        flushBatch();
        const auto sampleCount = packet->pcmSamples.size();
        AudioSourceBlock block;
        block.contractVersion = packet->contractVersion;
        block.sampleRate = packet->sampleRate;
        block.channelCount = packet->channelCount;
        block.frameCounter = packet->frameCounter;
        block.firstSampleFrame = packet->firstSampleFrame;
        block.mixedSamples = std::move(packet->pcmSamples);
        block.voices = std::move(packet->voices);
        block.voiceStems = std::move(packet->voiceStems);
        block.events = std::move(packet->events);
        (void)service_->submitSourceBlock(std::move(block));
        flushCount_.fetch_add(1u, std::memory_order_relaxed);
        flushLast_.store(sampleCount, std::memory_order_relaxed);
        atomicMinNonZero(flushMin_, sampleCount);
        atomicMax(flushMax_, sampleCount);
        packetsFlushed_.fetch_add(1u, std::memory_order_relaxed);
        return;
    }
    batchSamples_.insert(batchSamples_.end(), packet->pcmSamples.begin(), packet->pcmSamples.end());
    batchFrameCounter_ = packet->frameCounter;
    ++batchPackets_;
    currentSamples_.store(batchSamples_.size(), std::memory_order_relaxed);
    if (batchPackets_ >= batchChunks_) flushBatch();
}

void AudioTransportPlugin::flushBatch()
{
    if (service_ == nullptr || batchSamples_.empty()) return;
    // Detach may run before another audio event observes a reset/restart.
    // Never relabel a partial batch from the previous stream with the new epoch.
    if (batchEpoch_ != service_->lifecycleEpoch()) {
        discardBatch();
        return;
    }
    service_->appendRecentPcm(batchSamples_, batchFrameCounter_);
    flushCount_.fetch_add(1u, std::memory_order_relaxed);
    flushLast_.store(batchSamples_.size(), std::memory_order_relaxed);
    atomicMinNonZero(flushMin_, batchSamples_.size());
    atomicMax(flushMax_, batchSamples_.size());
    packetsFlushed_.fetch_add(batchPackets_, std::memory_order_relaxed);
    discardBatch();
}

AudioTransportPluginStats AudioTransportPlugin::stats() const noexcept
{
    return {
        .events = events_.load(std::memory_order_relaxed),
        .realtimePacketsAccepted = accepted_.load(std::memory_order_relaxed),
        .realtimePacketsSkipped = skipped_.load(std::memory_order_relaxed),
        .batchFlushCount = flushCount_.load(std::memory_order_relaxed),
        .batchFlushSamplesLast = flushLast_.load(std::memory_order_relaxed),
        .batchFlushSamplesMin = flushMin_.load(std::memory_order_relaxed),
        .batchFlushSamplesMax = flushMax_.load(std::memory_order_relaxed),
        .batchPacketsAccumulated = packetsAccumulated_.load(std::memory_order_relaxed),
        .batchPacketsFlushed = packetsFlushed_.load(std::memory_order_relaxed),
        .batchCurrentSamples = currentSamples_.load(std::memory_order_relaxed),
        .packetSamplesLast = packetSamplesLast_.load(std::memory_order_relaxed),
        .packetSamplesMin = packetSamplesMin_.load(std::memory_order_relaxed),
        .packetSamplesMax = packetSamplesMax_.load(std::memory_order_relaxed),
        .sampleRateLast = sampleRateLast_.load(std::memory_order_relaxed),
        .channelCountLast = channelCountLast_.load(std::memory_order_relaxed),
        .psgChunksEmittedLast = psgChunksEmittedLast_.load(std::memory_order_relaxed),
        .psgSamplesGeneratedTotalLast =
            psgSamplesGeneratedTotalLast_.load(std::memory_order_relaxed),
        .psgChunkSamplesLast = psgChunkSamplesLast_.load(std::memory_order_relaxed),
        .psgChunkSamplesMin = psgChunkSamplesMin_.load(std::memory_order_relaxed),
        .psgChunkSamplesMax = psgChunkSamplesMax_.load(std::memory_order_relaxed),
        .psgPendingSamplesLast = psgPendingSamplesLast_.load(std::memory_order_relaxed),
    };
}

} // namespace BMMQ
