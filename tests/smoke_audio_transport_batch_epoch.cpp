#include <algorithm>
#include <array>
#include <iostream>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/audio/AudioTransportPlugin.hpp"

namespace {
// Supply legacy PCM packets while retaining a real machine context and service.
class PcmMachine final : public BMMQ::Machine {
public:
    GB::GameBoyMachine core;
    BMMQ::PluginManager plugins;
    std::int16_t sample = 1111;
    void loadRom(const std::vector<uint8_t>& rom) override { core.loadRom(rom); }
    BMMQ::RuntimeContext& runtimeContext() override { return core.runtimeContext(); }
    const BMMQ::RuntimeContext& runtimeContext() const override { return core.runtimeContext(); }
    BMMQ::PluginManager& pluginManager() override { return plugins; }
    const BMMQ::PluginManager& pluginManager() const override { return plugins; }
    std::span<const BMMQ::IoRegionDescriptor> describeIoRegions() const override { return core.describeIoRegions(); }
    void attachExecutorPolicy(const BMMQ::Plugin::IExecutorPolicyPlugin& policy) override { core.attachExecutorPolicy(policy); }
    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return core.attachedExecutorPolicy(); }
    uint16_t readRegisterPair(std::string_view id) const override { return core.readRegisterPair(id); }
    std::optional<BMMQ::RealtimeAudioPacket> realtimeAudioPacket() const override {
        BMMQ::RealtimeAudioPacket packet;
        packet.sampleRate = 48000;
        packet.channelCount = 1;
        packet.pcmSamples.assign(256u, sample);
        return packet;
    }
};
}

int main()
{
    for (unsigned boundary = 0u; boundary < 4u; ++boundary) {
        PcmMachine machine;
        auto& plugin = machine.plugins.add(std::make_unique<BMMQ::AudioTransportPlugin>(2u));
        auto view = machine.mutableView();
        machine.plugins.initialize(view);
        const BMMQ::MachineEvent event{BMMQ::MachineEventType::AudioFrameReady, BMMQ::PluginCategory::Audio};
        machine.plugins.emit(view, event);
        if (plugin.stats().batchCurrentSamples != 256u) return 1;
        if (boundary < 2u) machine.audioService().bumpLifecycleEpochBarrier();
        if (boundary == 2u) {
            machine.plugins.emit(view, BMMQ::MachineEvent{
                BMMQ::MachineEventType::RomLoaded, BMMQ::PluginCategory::System});
        }
        if (boundary == 1u || boundary == 3u) {
            machine.plugins.shutdown(view);
            if (machine.audioService().transportStats().sourceQueueDepth != (boundary == 3u ? 1u : 0u) ||
                plugin.stats().batchCurrentSamples != 0u) {
                std::cerr << "detach submitted a stale PCM batch after epoch change\n";
                return 1;
            }
            continue;
        }
        machine.sample = 4321;
        machine.plugins.emit(view, event);
        machine.plugins.emit(view, event);
        std::array<int16_t, 512> output{};
        machine.audioService().renderForOutput(output);
        if (!std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 4321; })) {
            std::cerr << "PCM from an earlier epoch leaked into the new stream\n";
            return 1;
        }
        machine.plugins.shutdown(view);
    }
    return 0;
}
