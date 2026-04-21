
#ifndef GAMEGEAR_MACHINE_HPP
#define GAMEGEAR_MACHINE_HPP

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "machine/Machine.hpp"

namespace BMMQ {


class GameGearMachine final : public Machine {
public:
    GameGearMachine();
    ~GameGearMachine() override;

    void loadRom(const std::vector<uint8_t>& bytes) override;
    RuntimeContext& runtimeContext() override;
    const RuntimeContext& runtimeContext() const override;
    PluginManager& pluginManager() override;
    const PluginManager& pluginManager() const override;
    void step() override;

    std::span<const IoRegionDescriptor> describeIoRegions() const override;
    void attachExecutorPolicy(Plugin::IExecutorPolicyPlugin& policy) override;
    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override;
    uint16_t readRegisterPair(std::string_view id) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace BMMQ

#endif // GAMEGEAR_MACHINE_HPP
