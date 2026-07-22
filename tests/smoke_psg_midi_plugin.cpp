#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/audio/PsgMidiPlugin.hpp"

namespace {

class RecordingMidiSink final : public BMMQ::IMidiMessageSink {
public:
    void send(const BMMQ::MidiMessage& message) override { messages.push_back(message); }
    void flush() override { flushed = true; }

    std::vector<BMMQ::MidiMessage> messages;
    bool flushed = false;
};

class PacketConsumer final : public BMMQ::IAudioPlugin {
public:
    std::string_view id() const override { return "test.psg-midi.packet-consumer"; }
    void onAudioEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override
    {
        const auto packet = view.realtimeAudioPacket();
        if (packet.has_value()) eventCount += packet->events.size();
    }
    std::size_t eventCount = 0u;
};

} // namespace

int main()
{
    BMMQ::GameGearMachine machine;
    std::vector<std::uint8_t> rom(0x4000u, 0x00u);
    // Tone 0: period 0x031, maximum level, then idle.
    rom[0x0000u] = 0x3Eu; rom[0x0001u] = 0x81u;
    rom[0x0002u] = 0xD3u; rom[0x0003u] = 0x7Eu;
    rom[0x0004u] = 0x3Eu; rom[0x0005u] = 0x03u;
    rom[0x0006u] = 0xD3u; rom[0x0007u] = 0x7Fu;
    rom[0x0008u] = 0x3Eu; rom[0x0009u] = 0x90u;
    rom[0x000Au] = 0xD3u; rom[0x000Bu] = 0x40u;
    rom[0x000Cu] = 0xC3u; rom[0x000Du] = 0x0Cu; rom[0x000Eu] = 0x00u;
    machine.loadRom(rom);

    auto sink = std::make_unique<RecordingMidiSink>();
    auto* recording = sink.get();
    machine.pluginManager().add(std::make_unique<BMMQ::PsgMidiPlugin>(std::move(sink)));
    machine.pluginManager().initialize(machine.mutableView());

    for (int i = 0; i < 50000 && machine.audioFrameCounter() < 1u; ++i) machine.step();

    bool sawNoteOn = false;
    bool sawPitchBend = false;
    for (const auto& message : recording->messages) {
        sawNoteOn = sawNoteOn || ((message.status & 0xF0u) == 0x90u && message.data2 != 0u);
        sawPitchBend = sawPitchBend || ((message.status & 0xF0u) == 0xE0u);
        assert(message.sampleRate == 48000u);
    }
    assert(sawNoteOn);
    assert(sawPitchBend);

    machine.pluginManager().shutdown(machine.mutableView());
    assert(recording->flushed);
    bool sawNoteOff = false;
    for (const auto& message : recording->messages) {
        sawNoteOff = sawNoteOff || ((message.status & 0xF0u) == 0x80u);
    }
    assert(sawNoteOff);

    // Game Boy pending audio is consumptive internally. Verify the immutable
    // packet cache lets a normal audio client and MIDI translator both see it.
    GB::GameBoyMachine gameBoy;
    gameBoy.loadRom(std::vector<std::uint8_t>(0x8000u, 0x00u));
    auto consumer = std::make_unique<PacketConsumer>();
    auto* packetConsumer = consumer.get();
    gameBoy.pluginManager().add(std::move(consumer));
    auto gbSink = std::make_unique<RecordingMidiSink>();
    auto* gbRecording = gbSink.get();
    gameBoy.pluginManager().add(std::make_unique<BMMQ::PsgMidiPlugin>(std::move(gbSink)));
    gameBoy.pluginManager().initialize(gameBoy.mutableView());
    gameBoy.runtimeContext().write8(0xFF26u, 0x80u);
    gameBoy.runtimeContext().write8(0xFF24u, 0x77u);
    gameBoy.runtimeContext().write8(0xFF25u, 0xFFu);
    gameBoy.runtimeContext().write8(0xFF11u, 0x80u);
    gameBoy.runtimeContext().write8(0xFF12u, 0xF0u);
    gameBoy.runtimeContext().write8(0xFF13u, 0x70u);
    gameBoy.runtimeContext().write8(0xFF14u, 0x87u);
    for (int i = 0; i < 100000 && gameBoy.audioFrameCounter() < 1u; ++i) gameBoy.step();
    assert(packetConsumer->eventCount > 0u);
    assert(!gbRecording->messages.empty());
    gameBoy.pluginManager().shutdown(gameBoy.mutableView());

    const auto midiPath = std::filesystem::temp_directory_path() / "proto-time-psg-midi-smoke.mid";
    BMMQ::MidiFileSink fileSink(midiPath);
    fileSink.send({0u, 48000u, 0x90u, 69u, 100u, 3u});
    fileSink.send({48000u, 48000u, 0x80u, 69u, 0u, 3u});
    fileSink.flush();
    std::ifstream input(midiPath, std::ios::binary);
    std::vector<char> header(14u, 0);
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    assert(input.gcount() == static_cast<std::streamsize>(header.size()));
    assert(header[0] == 'M' && header[1] == 'T' && header[2] == 'h' && header[3] == 'd');
    std::filesystem::remove(midiPath);

    auto workerTarget = std::make_unique<RecordingMidiSink>();
    auto* workerRecording = workerTarget.get();
    BMMQ::AsyncMidiSink asyncSink(std::move(workerTarget));
    asyncSink.send({0u, 48000u, 0x90u, 60u, 100u, 3u});
    asyncSink.send({1u, 48000u, 0x80u, 60u, 0u, 3u});
    asyncSink.flush();
    const auto asyncStats = asyncSink.stats();
    assert(asyncStats.enqueued == 2u);
    assert(asyncStats.sent == 2u);
    assert(asyncStats.dropped == 0u);
    assert(asyncStats.errors == 0u);
    assert(workerRecording->messages.size() == 2u);
    assert(workerRecording->flushed);
    return 0;
}
