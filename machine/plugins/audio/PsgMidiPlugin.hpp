#ifndef BMMQ_PSG_MIDI_PLUGIN_HPP
#define BMMQ_PSG_MIDI_PLUGIN_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "machine/plugins/IoPlugin.hpp"

namespace BMMQ {

struct MidiMessage {
    std::uint64_t sampleFrame = 0u;
    std::uint32_t sampleRate = 48000u;
    std::uint8_t status = 0u;
    std::uint8_t data1 = 0u;
    std::uint8_t data2 = 0u;
    std::uint8_t size = 3u;
};

class IMidiMessageSink {
public:
    virtual ~IMidiMessageSink() = default;
    // Called on the emulation event lane. Live-device sinks must enqueue into
    // bounded storage and return without blocking; they must not perform OS I/O here.
    virtual void send(const MidiMessage& message) = 0;
    virtual void flush() {}
};

// Collects channel messages and writes a format-0 Standard MIDI File on flush.
// File I/O is intentionally deferred until the plugin is detached.
class MidiFileSink final : public IMidiMessageSink {
public:
    explicit MidiFileSink(std::filesystem::path path);

    void send(const MidiMessage& message) override;
    void flush() override;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::size_t messageCount() const noexcept { return messages_.size(); }

private:
    std::filesystem::path path_;
    std::vector<MidiMessage> messages_;
    bool flushed_ = false;
};

class PsgMidiPlugin final : public IAudioPlugin {
public:
    explicit PsgMidiPlugin(std::unique_ptr<IMidiMessageSink> sink);

    [[nodiscard]] std::string_view id() const override { return "bmmq.audio.psg-midi"; }
    [[nodiscard]] std::string_view displayName() const override { return "PSG MIDI translator"; }

    void onAttach(MutableMachineView&) override;
    void onDetach(MutableMachineView&) override;
    void onMachineEvent(const MachineEvent&, const MachineView&) override {}
    void onAudioEvent(const MachineEvent& event, const MachineView& view) override;

private:
    struct VoiceState {
        bool active = false;
        bool gate = false;
        std::uint8_t note = 0u;
        std::uint8_t channel = 0u;
        std::uint8_t velocity = 1u;
        std::uint8_t timbre = 0u;
    };

    void processEvent(const PsgAudioEvent& event, std::uint64_t sampleFrame,
                      std::uint32_t sampleRate);
    void emit(std::uint64_t sampleFrame, std::uint32_t sampleRate,
              std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
              std::uint8_t size = 3u);
    void noteOn(VoiceState& voice, const PsgAudioEvent& event,
                std::uint64_t sampleFrame, std::uint32_t sampleRate);
    void noteOff(VoiceState& voice, std::uint64_t sampleFrame,
                 std::uint32_t sampleRate);

    std::unique_ptr<IMidiMessageSink> sink_;
    std::array<VoiceState, 256u> voices_{};
    std::uint64_t lastSampleFrame_ = 0u;
    std::uint32_t lastSampleRate_ = 48000u;
};

} // namespace BMMQ

#endif // BMMQ_PSG_MIDI_PLUGIN_HPP
