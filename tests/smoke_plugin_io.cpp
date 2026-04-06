#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/LoggingPlugins.hpp"
#include "machine/plugins/PluginManager.hpp"

namespace {

struct RecordingVideoPlugin final : BMMQ::IVideoPlugin {
    int attachCount = 0;
    int detachCount = 0;
    int machineEventCount = 0;
    int videoEventCount = 0;
    uint8_t lastObservedValue = 0;
    std::optional<BMMQ::VideoStateView> lastVideoState;
    BMMQ::MachineEvent lastMachineEvent{};
    BMMQ::MachineEvent lastVideoEvent{};

    std::string_view id() const override {
        return "test.video.recorder";
    }

    void onAttach(const BMMQ::MachineView& view) override {
        ++attachCount;
        assert(!view.ioRegions.empty());
    }

    void onDetach(const BMMQ::MachineView&) override {
        ++detachCount;
    }

    void onMachineEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView&) override {
        ++machineEventCount;
        lastMachineEvent = event;
    }

    void onVideoEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override {
        ++videoEventCount;
        lastVideoEvent = event;
        lastObservedValue = view.read8(event.address);
        lastVideoState = view.videoState();
    }
};

struct RecordingAudioPlugin final : BMMQ::IAudioPlugin {
    int audioEventCount = 0;
    std::optional<BMMQ::AudioStateView> lastAudioState;

    std::string_view id() const override {
        return "test.audio.recorder";
    }

    void onAudioEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++audioEventCount;
        lastAudioState = view.audioState();
    }
};

struct RecordingSerialPlugin final : BMMQ::ISerialPlugin {
    int serialEventCount = 0;
    std::optional<BMMQ::SerialStateView> lastSerialState;

    std::string_view id() const override {
        return "test.serial.recorder";
    }

    void onSerialEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++serialEventCount;
        lastSerialState = view.serialState();
    }
};

struct RecordingDigitalInputPlugin final : BMMQ::IDigitalInputPlugin {
    int eventCount = 0;
    std::optional<BMMQ::DigitalInputStateView> lastInputState;

    std::string_view id() const override {
        return "test.input.recorder";
    }

    void onDigitalInputEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++eventCount;
        lastInputState = view.digitalInputState();
    }
};

struct RecordingNetworkPlugin final : BMMQ::INetworkPlugin {
    int eventCount = 0;
    std::optional<BMMQ::NetworkStateView> lastState;

    std::string_view id() const override {
        return "test.network.recorder";
    }

    void onNetworkEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++eventCount;
        lastState = view.networkState();
    }
};

struct RecordingParallelPlugin final : BMMQ::IParallelPlugin {
    int eventCount = 0;
    std::optional<BMMQ::ParallelStateView> lastState;

    std::string_view id() const override {
        return "test.parallel.recorder";
    }

    void onParallelEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++eventCount;
        lastState = view.parallelState();
    }
};

struct ThrowingPlugin final : BMMQ::IPlugin {
    int attachCount = 0;
    int machineEventCount = 0;

    std::string_view id() const override {
        return "test.plugin.throwing";
    }

    void onAttach(const BMMQ::MachineView&) override {
        ++attachCount;
    }

    void onMachineEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView&) override {
        ++machineEventCount;
        throw std::runtime_error("simulated plugin failure");
    }
};

struct FixedDigitalInputPlugin final : BMMQ::IDigitalInputPlugin {
    explicit FixedDigitalInputPlugin(uint32_t state) : state(state) {}

    uint32_t state = 0;
    int sampleCalls = 0;

    std::string_view id() const override {
        return "test.input.fixed";
    }

    std::optional<uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override {
        ++sampleCalls;
        return state;
    }
};

} // namespace

int main()
{
    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x00;
    machine.loadRom(cartridgeRom);

    auto videoPlugin = std::make_unique<RecordingVideoPlugin>();
    auto* video = videoPlugin.get();
    auto loggingVideoPlugin = std::make_unique<BMMQ::LoggingVideoPlugin>();
    auto* loggingVideo = loggingVideoPlugin.get();
    auto audioPlugin = std::make_unique<RecordingAudioPlugin>();
    auto* audio = audioPlugin.get();
    auto loggingAudioPlugin = std::make_unique<BMMQ::LoggingAudioPlugin>();
    auto* loggingAudio = loggingAudioPlugin.get();
    auto serialPlugin = std::make_unique<RecordingSerialPlugin>();
    auto* serial = serialPlugin.get();
    auto digitalInputPlugin = std::make_unique<RecordingDigitalInputPlugin>();
    auto* inputRecorder = digitalInputPlugin.get();
    auto loggingInputPlugin = std::make_unique<BMMQ::LoggingDigitalInputPlugin>();
    auto* loggingInput = loggingInputPlugin.get();
    auto networkPlugin = std::make_unique<RecordingNetworkPlugin>();
    auto* network = networkPlugin.get();
    auto parallelPlugin = std::make_unique<RecordingParallelPlugin>();
    auto* parallel = parallelPlugin.get();
    auto throwingPlugin = std::make_unique<ThrowingPlugin>();
    auto* throwing = throwingPlugin.get();
    auto firstInputPlugin = std::make_unique<FixedDigitalInputPlugin>(0x0Fu);
    auto* firstInput = firstInputPlugin.get();
    auto secondInputPlugin = std::make_unique<FixedDigitalInputPlugin>(0xF0u);
    auto* secondInput = secondInputPlugin.get();

    machine.pluginManager().add(std::move(videoPlugin));
    machine.pluginManager().add(std::move(loggingVideoPlugin));
    machine.pluginManager().add(std::move(audioPlugin));
    machine.pluginManager().add(std::move(loggingAudioPlugin));
    machine.pluginManager().add(std::move(serialPlugin));
    machine.pluginManager().add(std::move(digitalInputPlugin));
    machine.pluginManager().add(std::move(loggingInputPlugin));
    machine.pluginManager().add(std::move(networkPlugin));
    machine.pluginManager().add(std::move(parallelPlugin));
    machine.pluginManager().add(std::move(throwingPlugin));
    machine.pluginManager().add(std::move(firstInputPlugin));
    machine.pluginManager().add(std::move(secondInputPlugin));

    assert(machine.pluginManager().size() == 12);
    assert(!machine.describeIoRegions().empty());

    machine.pluginManager().initialize(machine.view());
    assert(video->attachCount == 1);
    assert(throwing->attachCount == 1);

    const auto sampledInput = machine.pluginManager().sampleDigitalInput(machine.view());
    assert(sampledInput.has_value());
    assert(*sampledInput == 0x0Fu);
    assert(firstInput->sampleCalls == 1);
    assert(secondInput->sampleCalls == 0);

    machine.setJoypadState(0x03u);
    assert(video->machineEventCount >= 1);
    assert(inputRecorder->eventCount >= 1);
    assert(inputRecorder->lastInputState.has_value());
    assert(inputRecorder->lastInputState->pressedMask == 0x03u);
    assert(inputRecorder->lastInputState->joypRegister == machine.runtimeContext().read8(0xFF00));
    assert(inputRecorder->lastInputState->directionPressed());
    assert(loggingInput->entryCount() >= 1);

    machine.runtimeContext().write8(0x8000, 0x42u);
    machine.runtimeContext().write8(0xFF40, 0x91u);
    assert(video->videoEventCount >= 2);
    assert(video->lastVideoEvent.type == BMMQ::MachineEventType::MemoryWriteObserved);
    assert(video->lastVideoEvent.category == BMMQ::PluginCategory::Video);
    assert(video->lastObservedValue == 0x91u);
    assert(video->lastVideoState.has_value());
    assert(video->lastVideoState->lcdc == 0x91u);
    assert(video->lastVideoState->lcdEnabled());
    assert(video->lastVideoState->vram.size() == 0x2000u);
    assert(video->lastVideoState->oam.size() == 0x00A0u);
    assert(video->lastVideoState->vram[0] == 0x42u);
    assert(loggingVideo->entryCount() >= 2);

    machine.runtimeContext().write8(0xFF12, 0xF3u);
    assert(audio->audioEventCount >= 1);
    assert(audio->lastAudioState.has_value());
    assert(audio->lastAudioState->nr12 == 0xF3u);
    assert(audio->lastAudioState->registers.size() == 0x17u);
    assert(audio->lastAudioState->registers[0x02] == 0xF3u);
    assert(loggingAudio->entryCount() >= 1);

    machine.runtimeContext().write8(0xFF01, 0xABu);
    machine.runtimeContext().write8(0xFF02, 0x81u);
    assert(serial->serialEventCount >= 2);
    assert(serial->lastSerialState.has_value());
    assert(serial->lastSerialState->sb == 0xABu);
    assert(serial->lastSerialState->sc == 0x81u);
    assert(serial->lastSerialState->transferRequested());

    machine.pluginManager().emit(machine.view(), {
        BMMQ::MachineEventType::NetworkActivity,
        BMMQ::PluginCategory::Network,
        0,
        0,
        0,
        nullptr,
        "placeholder network event"
    });
    machine.pluginManager().emit(machine.view(), {
        BMMQ::MachineEventType::ParallelActivity,
        BMMQ::PluginCategory::Parallel,
        0,
        0,
        0,
        nullptr,
        "placeholder parallel event"
    });
    assert(network->eventCount >= 1);
    assert(network->lastState.has_value());
    assert(network->lastState->connected == false);
    assert(parallel->eventCount >= 1);
    assert(parallel->lastState.has_value());
    assert(parallel->lastState->busy == false);

    machine.step();
    assert(machine.pluginManager().disabledCount() >= 1);
    assert(throwing->machineEventCount == 1);
    assert(firstInput->sampleCalls == 2);
    assert(video->lastMachineEvent.type == BMMQ::MachineEventType::StepCompleted);

    machine.step();
    assert(machine.pluginManager().disabledCount() >= 1);
    assert(throwing->machineEventCount == 1);
    assert(video->machineEventCount >= 2);

    machine.pluginManager().shutdown(machine.view());
    assert(video->detachCount == 1);

    return 0;
}
