#include "GameGearMachine.hpp"
#include <array>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include "inst_cycle/executor/PluginContract.hpp"
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
constexpr std::array<IoRegionDescriptor, 3> kIoRegions{{
    {PluginCategory::System, 0x0000u, 0xC000u, "rom", true, false},
    {PluginCategory::System, 0xC000u, 0x2000u, "ram", true, true},
    {PluginCategory::DigitalInput, 0x00DCu, 0x0001u, "input", true, false},
}};

class GameGearRuntimeContext final : public RuntimeContext {
public:
    GameGearRuntimeContext(Z80Interpreter& cpu,
                           GameGearMemoryMap& memoryMap,
                           bool& romLoaded,
                           Plugin::IExecutorPolicyPlugin*& activePolicy)
        : cpu_(cpu),
          memoryMap_(memoryMap),
          romLoaded_(romLoaded),
          activePolicy_(activePolicy)
    {
    }

    FetchBlock fetch() override {
        FetchBlock block;
        block.setbaseAddress(cpu_.PC);
        auto& entries = block.getblockData();
        entries.push_back(fetchBlockData<AddressType, DataType>{cpu_.PC, {memoryMap_.read(cpu_.PC)}});
        return block;
    }

    ExecutionBlock decode(FetchBlock&) override {
        ExecutionBlock block;
        return block;
    }

    void execute(const ExecutionBlock&, FetchBlock&) override {
        step();
    }

    CpuFeedback step(FetchBlock&) override {
        return step();
    }

    CpuFeedback step() override {
        if (!romLoaded_) {
            return lastFeedback_;
        }
        lastFeedback_.pcBefore = cpu_.PC;
        const auto retiredCycles = cpu_.step();
        lastFeedback_.pcAfter = cpu_.PC;
        lastFeedback_.retiredCycles = retiredCycles;
        lastFeedback_.segmentBoundaryHint = false;
        lastFeedback_.isControlFlow = false;
        lastFeedback_.executionPath = ExecutionPathHint::CanonicalFetchDecodeExecute;
        return lastFeedback_;
    }

    DataType read8(AddressType address) const override {
        return memoryMap_.read(address);
    }

    void write8(AddressType address, DataType value) override {
        memoryMap_.write(address, value);
    }

    uint8_t readRegister8(std::string_view id) const override {
        if (id == "A") {
            return static_cast<uint8_t>((cpu_.AF >> 8) & 0x00FFu);
        }
        if (id == "F") {
            return static_cast<uint8_t>(cpu_.AF & 0x00FFu);
        }
        throw std::invalid_argument("register not found");
    }

    void writeRegister8(std::string_view id, uint8_t value) override {
        if (id == "A") {
            cpu_.AF = static_cast<uint16_t>((cpu_.AF & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
            return;
        }
        if (id == "F") {
            cpu_.AF = static_cast<uint16_t>((cpu_.AF & 0xFF00u) | value);
            return;
        }
        throw std::invalid_argument("register not found");
    }

    uint16_t readRegister16(std::string_view id) const override {
        return registerRefConst(id);
    }

    void writeRegister16(std::string_view id, uint16_t value) override {
        registerRef(id) = value;
    }

    const CpuFeedback& getLastFeedback() const override {
        return lastFeedback_;
    }

    uint32_t clockHz() const override {
        return 3579545u;
    }

    ExecutionGuarantee guarantee() const override {
        return activePolicy_->guarantee();
    }

    const Plugin::PluginMetadata* attachedPolicyMetadata() const override {
        return &activePolicy_->metadata();
    }

    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

private:
    uint16_t& registerRef(std::string_view id) {
        if (id == "AF") return cpu_.AF;
        if (id == "BC") return cpu_.BC;
        if (id == "DE") return cpu_.DE;
        if (id == "HL") return cpu_.HL;
        if (id == "IX") return cpu_.IX;
        if (id == "IY") return cpu_.IY;
        if (id == "SP") return cpu_.SP;
        if (id == "PC") return cpu_.PC;
        throw std::invalid_argument("register not found");
    }

    const uint16_t& registerRefConst(std::string_view id) const {
        if (id == "AF") return cpu_.AF;
        if (id == "BC") return cpu_.BC;
        if (id == "DE") return cpu_.DE;
        if (id == "HL") return cpu_.HL;
        if (id == "IX") return cpu_.IX;
        if (id == "IY") return cpu_.IY;
        if (id == "SP") return cpu_.SP;
        if (id == "PC") return cpu_.PC;
        throw std::invalid_argument("register not found");
    }

    Z80Interpreter& cpu_;
    GameGearMemoryMap& memoryMap_;
    bool& romLoaded_;
    Plugin::IExecutorPolicyPlugin*& activePolicy_;
    CpuFeedback lastFeedback_{};
};
}

struct GameGearMachine::Impl {
    Z80Interpreter cpu;
    GameGearVDP vdp;
    GameGearPSG psg;
    GameGearInput input;
    GameGearCartridge cart;
    GameGearMemoryMap mem;
    PluginManager pluginManager;
    Plugin::DefaultStepPolicy defaultPolicy;
    Plugin::IExecutorPolicyPlugin* activePolicy = &defaultPolicy;
    bool romLoaded = false;
    std::optional<uint32_t> lastDigitalInputMask;
    uint64_t inputGeneration = 0u;
    GameGearRuntimeContext context{cpu, mem, romLoaded, activePolicy};
};

std::span<const IoRegionDescriptor> GameGearMachine::describeIoRegions() const {
    return kIoRegions;
}

void GameGearMachine::attachExecutorPolicy(Plugin::IExecutorPolicyPlugin& policy) {
    Plugin::validateExecutorPolicyStartup(policy);
    impl->activePolicy = &policy;
}

const Plugin::IExecutorPolicyPlugin& GameGearMachine::attachedExecutorPolicy() const {
    return *impl->activePolicy;
}

uint16_t GameGearMachine::readRegisterPair(std::string_view name) const {
    return impl->context.readRegister16(name);
}

GameGearMachine::GameGearMachine() : impl(std::make_unique<Impl>()) {
    // Wire up CPU memory interface to memory map
    impl->cpu.setMemoryInterface(
        [this](uint16_t addr) { return impl->mem.read(addr); },
        [this](uint16_t addr, uint8_t val) { impl->mem.write(addr, val); }
    );
    impl->mem.setInput(&impl->input);
    Plugin::validateExecutorPolicyStartup(impl->defaultPolicy);
    impl->cpu.reset();
}
GameGearMachine::~GameGearMachine() {
    if (impl->pluginManager.initialized()) {
        impl->pluginManager.shutdown(view());
    }
}

void GameGearMachine::loadRom(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot load empty ROM");
    }
    if (bytes.size() > kMaxRomSize) {
        throw std::runtime_error("ROM too large");
    }
    impl->mem.mapRom(bytes.data(), bytes.size());
    impl->cart.load(bytes.data(), bytes.size());
    impl->romLoaded = true;
    ++impl->inputGeneration;
    inputService().advanceGeneration(impl->inputGeneration);
    impl->lastDigitalInputMask.reset();
    impl->cpu.reset();
    if (impl->pluginManager.size() != 0u) {
        impl->pluginManager.emit(view(), MachineEvent{
            MachineEventType::RomLoaded,
            PluginCategory::System,
            0u,
            0u,
            0u,
            nullptr,
            "ROM loaded"
        });
    }
}

RuntimeContext& GameGearMachine::runtimeContext() {
    return impl->context;
}

const RuntimeContext& GameGearMachine::runtimeContext() const {
    return impl->context;
}

PluginManager& GameGearMachine::pluginManager() {
    return impl->pluginManager;
}

const PluginManager& GameGearMachine::pluginManager() const {
    return impl->pluginManager;
}

void GameGearMachine::step() {
    if (!impl->romLoaded) {
        return;
    }
    impl->context.step();
    impl->vdp.step();
    impl->psg.step();
}

void GameGearMachine::serviceInput() {
    if (inputService().state() == InputLifecycleState::Active) {
        (void)inputService().pollActiveAdapter(impl->inputGeneration);
        if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
            const auto pressedMask = static_cast<uint8_t>(*committedInput & 0x00FFu);
            impl->input.setLogicalButtons(pressedMask);
            impl->lastDigitalInputMask = static_cast<uint32_t>(pressedMask);
            return;
        }
    }

    if (impl->pluginManager.size() == 0u) {
        return;
    }

    if (const auto sampledInput = impl->pluginManager.sampleDigitalInput(view()); sampledInput.has_value()) {
        const auto pressedMask = static_cast<uint8_t>(*sampledInput & 0x00FFu);
        impl->input.setLogicalButtons(pressedMask);
        impl->lastDigitalInputMask = static_cast<uint32_t>(pressedMask);
    }
}

std::optional<uint32_t> GameGearMachine::currentDigitalInputMask() const {
    return impl->lastDigitalInputMask;
}

uint32_t GameGearMachine::clockHz() const {
    return impl->context.clockHz();
}

std::string GameGearMachine::stopSummary() const {
    std::ostringstream out;
    out << "PC=0x" << std::hex << std::uppercase << impl->cpu.PC
        << " SP=0x" << impl->cpu.SP
        << " AF=0x" << impl->cpu.AF
        << std::dec << '\n'
        << "Input port=0x" << std::hex << std::uppercase << static_cast<int>(impl->input.readInputs())
        << std::dec;
    return out.str();
}

} // namespace BMMQ
