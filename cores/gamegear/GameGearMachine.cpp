#include "GameGearMachine.hpp"
#include <span>
#include <stdexcept>
#include <string_view>
#include "machine/plugins/IoPlugin.hpp"
#include "machine/plugins/PluginManager.hpp"
#include "Z80Interpreter.hpp"
#include "GameGearVDP.hpp"
#include "GameGearPSG.hpp"
#include "GameGearInput.hpp"
#include "GameGearCartridge.hpp"
#include "GameGearMemoryMap.hpp"

namespace BMMQ {

namespace {
constexpr std::size_t kMaxRomSize = 1024u * 1024u;
}

std::span<const IoRegionDescriptor> GameGearMachine::describeIoRegions() const {
    return std::span<const IoRegionDescriptor>{};
}

void GameGearMachine::attachExecutorPolicy(Plugin::IExecutorPolicyPlugin&) {
    throw std::runtime_error("Not implemented");
}

const Plugin::IExecutorPolicyPlugin& GameGearMachine::attachedExecutorPolicy() const {
    throw std::runtime_error("Not implemented");
}

uint16_t GameGearMachine::readRegisterPair(std::string_view name) const {
    // TODO: Implement register pair reading from Z80
    (void)name;
    throw std::runtime_error("readRegisterPair not implemented");
}

struct GameGearMachine::Impl {
    Z80Interpreter cpu;
    GameGearVDP vdp;
    GameGearPSG psg;
    GameGearInput input;
    GameGearCartridge cart;
    GameGearMemoryMap mem;
    // TODO: Add runtime context, plugin manager, etc.
};

GameGearMachine::GameGearMachine() : impl(std::make_unique<Impl>()) {
    // Wire up CPU memory interface to memory map
    impl->cpu.setMemoryInterface(
        [this](uint16_t addr) { return impl->mem.read(addr); },
        [this](uint16_t addr, uint8_t val) { impl->mem.write(addr, val); }
    );
    impl->mem.setInput(&impl->input);
    impl->cpu.reset();
}
GameGearMachine::~GameGearMachine() = default;
void GameGearMachine::loadRom(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot load empty ROM");
    }
    if (bytes.size() > kMaxRomSize) {
        throw std::runtime_error("ROM too large");
    }
    impl->mem.mapRom(bytes.data(), bytes.size());
    impl->cart.load(bytes.data(), bytes.size());
    impl->cpu.reset();
}

RuntimeContext& GameGearMachine::runtimeContext() {
    // TODO: Return actual runtime context
    throw std::runtime_error("Not implemented");
}

const RuntimeContext& GameGearMachine::runtimeContext() const {
    // TODO: Return actual runtime context
    throw std::runtime_error("Not implemented");
}

PluginManager& GameGearMachine::pluginManager() {
    // TODO: Return actual plugin manager
    throw std::runtime_error("Not implemented");
}

const PluginManager& GameGearMachine::pluginManager() const {
    // TODO: Return actual plugin manager
    throw std::runtime_error("Not implemented");
}

void GameGearMachine::step() {
    impl->cpu.step();
    impl->vdp.step();
    impl->psg.step();
    // TODO: Add timing and synchronization
}

} // namespace BMMQ
