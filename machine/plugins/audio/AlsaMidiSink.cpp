#include "AlsaMidiSink.hpp"

#include <alsa/asoundlib.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace BMMQ {
namespace {

[[noreturn]] void throwAlsa(std::string_view operation, int error)
{
    throw std::runtime_error(std::string(operation) + ": " + snd_strerror(error));
}

} // namespace

class AlsaMidiSink::Impl {
public:
    explicit Impl(AlsaMidiSinkConfig config)
    {
        auto result = snd_seq_open(&sequence_, "default", SND_SEQ_OPEN_OUTPUT, 0);
        if (result < 0) throwAlsa("Unable to open ALSA MIDI sequencer", result);
        try {
            result = snd_seq_set_client_name(sequence_, config.clientName.c_str());
            if (result < 0) throwAlsa("Unable to name ALSA MIDI client", result);
            port_ = snd_seq_create_simple_port(
                sequence_, config.portName.c_str(),
                SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
            if (port_ < 0) throwAlsa("Unable to create ALSA MIDI output port", port_);

            if (config.destination.has_value()) {
                snd_seq_addr_t address{};
                result = snd_seq_parse_address(sequence_, &address, config.destination->c_str());
                if (result < 0) throwAlsa("Invalid ALSA MIDI destination", result);
                result = snd_seq_connect_to(sequence_, port_, address.client, address.port);
                if (result < 0) throwAlsa("Unable to connect ALSA MIDI destination", result);
            }
        } catch (...) {
            snd_seq_close(sequence_);
            sequence_ = nullptr;
            throw;
        }
    }

    ~Impl()
    {
        if (sequence_ != nullptr) snd_seq_close(sequence_);
    }

    void send(const MidiMessage& message)
    {
        snd_seq_event_t event;
        snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event, port_);
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);

        const auto channel = static_cast<int>(message.status & 0x0Fu);
        switch (message.status & 0xF0u) {
        case 0x80u:
            snd_seq_ev_set_noteoff(&event, channel, message.data1, message.data2);
            break;
        case 0x90u:
            snd_seq_ev_set_noteon(&event, channel, message.data1, message.data2);
            break;
        case 0xB0u:
            snd_seq_ev_set_controller(&event, channel, message.data1, message.data2);
            break;
        case 0xC0u:
            snd_seq_ev_set_pgmchange(&event, channel, message.data1);
            break;
        case 0xE0u: {
            const auto bend = static_cast<int>(message.data1) |
                (static_cast<int>(message.data2) << 7);
            snd_seq_ev_set_pitchbend(&event, channel, bend - 8192);
            break;
        }
        default:
            return;
        }

        const auto result = snd_seq_event_output_direct(sequence_, &event);
        if (result < 0) throwAlsa("Unable to send ALSA MIDI event", result);
    }

    void flush()
    {
        const auto result = snd_seq_drain_output(sequence_);
        if (result < 0) throwAlsa("Unable to drain ALSA MIDI output", result);
    }

    [[nodiscard]] std::string sourceAddress() const
    {
        return std::to_string(snd_seq_client_id(sequence_)) + ":" + std::to_string(port_);
    }

private:
    snd_seq_t* sequence_ = nullptr;
    int port_ = -1;
};

AlsaMidiSink::AlsaMidiSink(AlsaMidiSinkConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

AlsaMidiSink::~AlsaMidiSink() = default;

void AlsaMidiSink::send(const MidiMessage& message)
{
    impl_->send(message);
}

void AlsaMidiSink::flush()
{
    impl_->flush();
}

std::string AlsaMidiSink::sourceAddress() const
{
    return impl_->sourceAddress();
}

} // namespace BMMQ
