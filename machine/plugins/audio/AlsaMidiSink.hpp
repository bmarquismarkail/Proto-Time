#ifndef BMMQ_ALSA_MIDI_SINK_HPP
#define BMMQ_ALSA_MIDI_SINK_HPP

#include <memory>
#include <optional>
#include <string>

#include "PsgMidiPlugin.hpp"

namespace BMMQ {

struct AlsaMidiSinkConfig {
    std::string clientName = "Proto-Time";
    std::string portName = "PSG MIDI Output";
    // ALSA sequencer address (for example "128:0"). When absent, the source
    // port broadcasts to subscribers connected with aconnect.
    std::optional<std::string> destination;
};

// Performs direct ALSA I/O. Wrap this sink in AsyncMidiSink before using it on
// the emulation event lane.
class AlsaMidiSink final : public IMidiMessageSink {
public:
    explicit AlsaMidiSink(AlsaMidiSinkConfig config = {});
    ~AlsaMidiSink() override;
    AlsaMidiSink(const AlsaMidiSink&) = delete;
    AlsaMidiSink& operator=(const AlsaMidiSink&) = delete;

    void send(const MidiMessage& message) override;
    void flush() override;
    [[nodiscard]] std::string sourceAddress() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace BMMQ

#endif // BMMQ_ALSA_MIDI_SINK_HPP
