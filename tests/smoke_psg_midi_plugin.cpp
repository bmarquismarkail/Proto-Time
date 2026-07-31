#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyAPU.hpp"
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

    // NR50 is master output gain, while NR51 is per-voice routing. Verify that
    // global writes publish one canonical event for every Game Boy voice and
    // that frame-sequencer envelope ticks publish level changes without a raw
    // register write.
    GB::GameBoyAPU apu;
    apu.reset();
    (void)apu.takePendingEvents(0u);
    apu.writeRegister(0xFF26u, 0x80u);
    apu.writeRegister(0xFF24u, 0x77u);
    apu.writeRegister(0xFF25u, 0xFFu);
    apu.writeRegister(0xFF11u, 0x80u);
    apu.writeRegister(0xFF12u, 0xF1u);
    apu.writeRegister(0xFF13u, 0x70u);
    apu.writeRegister(0xFF14u, 0x87u);
    (void)apu.takePendingEvents(0u);

    apu.writeRegister(0xFF24u, 0x33u);
    const auto masterEvents = apu.takePendingEvents(0u);
    std::array<bool, 4u> sawMasterVoice{};
    std::size_t masterLevelChanges = 0u;
    std::size_t masterRawWrites = 0u;
    for (const auto& event : masterEvents) {
        if (event.kind == BMMQ::PsgEventKind::LevelChange &&
            event.rawAddress == 0xFF24u) {
            sawMasterVoice[event.voiceId] = true;
            ++masterLevelChanges;
            if (event.voiceId == 0u) assert(event.levelQ15 == 16384u);
        }
        if (event.kind == BMMQ::PsgEventKind::RawWrite &&
            event.rawAddress == 0xFF24u) {
            ++masterRawWrites;
        }
    }
    assert(masterLevelChanges == 4u);
    assert(masterRawWrites == 1u);
    for (const auto sawVoice : sawMasterVoice) assert(sawVoice);

    apu.writeRegister(0xFF25u, 0x12u);
    const auto routingEvents = apu.takePendingEvents(0u);
    const std::array<std::uint8_t, 4u> expectedRouting{{1u, 2u, 0u, 0u}};
    std::array<bool, 4u> sawRoutingVoice{};
    for (const auto& event : routingEvents) {
        if (event.kind != BMMQ::PsgEventKind::RoutingChange ||
            event.rawAddress != 0xFF25u) {
            continue;
        }
        assert(event.routingMask == expectedRouting[event.voiceId]);
        sawRoutingVoice[event.voiceId] = true;
    }
    for (const auto sawVoice : sawRoutingVoice) assert(sawVoice);

    apu.writeRegister(0xFF24u, 0x77u);
    apu.writeRegister(0xFF25u, 0xFFu);
    (void)apu.takePendingEvents(0u);
    apu.step(8u * 8192u);
    const auto envelopeEvents = apu.takePendingEvents(0u);
    bool sawEnvelopeLevelChange = false;
    for (const auto& event : envelopeEvents) {
        if (event.kind == BMMQ::PsgEventKind::LevelChange &&
            event.voiceId == 0u && !event.hasRawWrite) {
            assert(event.levelQ15 == (14u * 32767u) / 15u);
            sawEnvelopeLevelChange = true;
        }
    }
    assert(sawEnvelopeLevelChange);

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
    gameBoy.runtimeContext().write8(0xFF12u, 0xF1u);
    gameBoy.runtimeContext().write8(0xFF13u, 0x70u);
    gameBoy.runtimeContext().write8(0xFF14u, 0x87u);
    for (int i = 0; i < 100000 && gameBoy.audioFrameCounter() < 1u; ++i) gameBoy.step();
    assert(packetConsumer->eventCount > 0u);
    assert(!gbRecording->messages.empty());

    bool sawMaximumVelocity = false;
    std::array<bool, 4u> sawVoicePan{};
    for (const auto& message : gbRecording->messages) {
        if ((message.status & 0xF0u) == 0x90u && (message.status & 0x0Fu) == 0u) {
            sawMaximumVelocity = sawMaximumVelocity || message.data2 == 127u;
        }
        if ((message.status & 0xF0u) == 0xB0u && message.data1 == 10u) {
            const auto channel = static_cast<std::uint8_t>(message.status & 0x0Fu);
            if (channel == 0u) sawVoicePan[0] = true;
            if (channel == 1u) sawVoicePan[1] = true;
            if (channel == 2u) sawVoicePan[2] = true;
            if (channel == 9u) sawVoicePan[3] = true;
        }
    }
    assert(sawMaximumVelocity);
    for (const auto sawPan : sawVoicePan) assert(sawPan);

    const auto messagesBeforeMasterFade = gbRecording->messages.size();
    const auto masterFadeFrame = gameBoy.audioFrameCounter() + 1u;
    gameBoy.runtimeContext().write8(0xFF24u, 0x33u);
    for (int i = 0; i < 100000 && gameBoy.audioFrameCounter() < masterFadeFrame; ++i) {
        gameBoy.step();
    }
    bool sawMasterExpression = false;
    for (std::size_t i = messagesBeforeMasterFade; i < gbRecording->messages.size(); ++i) {
        const auto& message = gbRecording->messages[i];
        sawMasterExpression = sawMasterExpression ||
            ((message.status & 0xF0u) == 0xB0u &&
             (message.status & 0x0Fu) == 0u &&
             message.data1 == 11u && message.data2 == 64u);
    }
    assert(sawMasterExpression);

    const auto messagesBeforeEnvelope = gbRecording->messages.size();
    const auto envelopeFrame = gameBoy.audioFrameCounter() + 4u;
    for (int i = 0; i < 200000 && gameBoy.audioFrameCounter() < envelopeFrame; ++i) {
        gameBoy.step();
    }
    bool sawEnvelopeExpression = false;
    for (std::size_t i = messagesBeforeEnvelope; i < gbRecording->messages.size(); ++i) {
        const auto& message = gbRecording->messages[i];
        sawEnvelopeExpression = sawEnvelopeExpression ||
            ((message.status & 0xF0u) == 0xB0u &&
             (message.status & 0x0Fu) == 0u &&
             message.data1 == 11u && message.data2 < 64u);
    }
    assert(sawEnvelopeExpression);

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
