#ifndef BMMQ_AUDIO_TRANSPORT_PLUGIN_HPP
#define BMMQ_AUDIO_TRANSPORT_PLUGIN_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "machine/plugins/IoPlugin.hpp"

namespace BMMQ {

struct AudioTransportPluginStats {
    std::size_t events = 0u;
    std::size_t realtimePacketsAccepted = 0u;
    std::size_t realtimePacketsSkipped = 0u;
    std::size_t batchFlushCount = 0u;
    std::size_t batchFlushSamplesLast = 0u;
    std::size_t batchFlushSamplesMin = 0u;
    std::size_t batchFlushSamplesMax = 0u;
    std::size_t batchPacketsAccumulated = 0u;
    std::size_t batchPacketsFlushed = 0u;
    std::size_t batchCurrentSamples = 0u;
    std::size_t packetSamplesLast = 0u;
    std::size_t packetSamplesMin = 0u;
    std::size_t packetSamplesMax = 0u;
    std::uint32_t sampleRateLast = 0u;
    std::uint8_t channelCountLast = 0u;
    std::uint64_t psgChunksEmittedLast = 0u;
    std::uint64_t psgSamplesGeneratedTotalLast = 0u;
    std::uint32_t psgChunkSamplesLast = 0u;
    std::uint32_t psgChunkSamplesMin = 0u;
    std::uint32_t psgChunkSamplesMax = 0u;
    std::uint32_t psgPendingSamplesLast = 0u;
};

class AudioTransportPlugin final : public IAudioPlugin {
public:
    explicit AudioTransportPlugin(std::size_t batchChunks = 1u)
        : batchChunks_(std::max<std::size_t>(batchChunks, 1u)) {}

    [[nodiscard]] std::string_view id() const override {
        return "bmmq.audio.transport.host";
    }

    void onAttach(MutableMachineView& view) override;
    void onDetach(MutableMachineView&) override;
    void onMachineEvent(const MachineEvent&, const MachineView&) override {}
    void onAudioEvent(const MachineEvent& event, const MachineView& view) override;
    [[nodiscard]] AudioTransportPluginStats stats() const noexcept;

private:
    template<typename T>
    static void atomicMax(std::atomic<T>& target, T value) noexcept;
    template<typename T>
    static void atomicMinNonZero(std::atomic<T>& target, T value) noexcept;
    void flushBatch();

    AudioService* service_ = nullptr;
    std::size_t batchChunks_ = 1u;
    std::vector<std::int16_t> batchSamples_;
    std::size_t batchPackets_ = 0u;
    std::uint64_t batchFrameCounter_ = 0u;
    std::atomic<std::size_t> events_{0u};
    std::atomic<std::size_t> accepted_{0u};
    std::atomic<std::size_t> skipped_{0u};
    std::atomic<std::size_t> flushCount_{0u};
    std::atomic<std::size_t> flushLast_{0u};
    std::atomic<std::size_t> flushMin_{0u};
    std::atomic<std::size_t> flushMax_{0u};
    std::atomic<std::size_t> packetsAccumulated_{0u};
    std::atomic<std::size_t> packetsFlushed_{0u};
    std::atomic<std::size_t> currentSamples_{0u};
    std::atomic<std::size_t> packetSamplesLast_{0u};
    std::atomic<std::size_t> packetSamplesMin_{0u};
    std::atomic<std::size_t> packetSamplesMax_{0u};
    std::atomic<std::uint32_t> sampleRateLast_{0u};
    std::atomic<std::uint8_t> channelCountLast_{0u};
    std::atomic<std::uint64_t> psgChunksEmittedLast_{0u};
    std::atomic<std::uint64_t> psgSamplesGeneratedTotalLast_{0u};
    std::atomic<std::uint32_t> psgChunkSamplesLast_{0u};
    std::atomic<std::uint32_t> psgChunkSamplesMin_{0u};
    std::atomic<std::uint32_t> psgChunkSamplesMax_{0u};
    std::atomic<std::uint32_t> psgPendingSamplesLast_{0u};
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_TRANSPORT_PLUGIN_HPP
