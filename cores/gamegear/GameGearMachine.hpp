
#ifndef GAMEGEAR_MACHINE_HPP
#define GAMEGEAR_MACHINE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "machine/Machine.hpp"

namespace BMMQ {


class GameGearMachine final : public Machine,
                              public IRomPathAwareMachine,
                              public IExternalBootRomMachine {
public:
    GameGearMachine();
    ~GameGearMachine() override;

    void loadRom(const std::vector<uint8_t>& bytes) override;
    void setRomSourcePath(const std::optional<std::filesystem::path>& path) override;
    RuntimeContext& runtimeContext() override;
    const RuntimeContext& runtimeContext() const override;
    PluginManager& pluginManager() override;
    const PluginManager& pluginManager() const override;
    void step() override;
    void serviceInput() override;

    std::span<const IoRegionDescriptor> describeIoRegions() const override;
    void attachExecutorPolicy(Plugin::IExecutorPolicyPlugin& policy) override;
    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override;
    uint16_t readRegisterPair(std::string_view id) const override;
    std::optional<uint32_t> currentDigitalInputMask() const override;
    std::vector<int16_t> recentAudioSamples() const override;
    uint32_t audioSampleRate() const override;
    uint64_t audioFrameCounter() const override;
    std::optional<VideoDebugFrameModel> videoDebugFrameModel(
        const VideoDebugRenderRequest& request) const override;
    uint32_t clockHz() const override;
    std::string stopSummary() const override;
    [[nodiscard]] bool flushCartridgeSave();
    void loadExternalBootRom(const std::vector<uint8_t>& bytes) override;
    // Test helper: inspect whether CPU IME is currently set.
    bool cpuInterruptsEnabled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace BMMQ

#endif // GAMEGEAR_MACHINE_HPP
