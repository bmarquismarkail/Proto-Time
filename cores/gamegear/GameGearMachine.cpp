#include "GameGearMachine.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
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
#include "GameGearMapperFactory.hpp"
#include "GameGearMapper.hpp"
#include "GameGearCartridge.hpp"
#include <memory>
#include "GameGearMemoryMap.hpp"
#include "GameGearSaveManager.hpp"

namespace BMMQ {

namespace {
constexpr std::size_t kMaxRomSize = 1024u * 1024u;
constexpr uint8_t kGameGearViewportY = 24u;
constexpr uint8_t kGameGearViewportHeight = 144u;
// Expose only true Game Gear memory regions and port-based IO descriptors.
// Remove Game Boy memory-mapped video/input descriptors so tools see port IO.
constexpr std::array<IoRegionDescriptor, 5> kIoRegions{{
    {PluginCategory::System, 0x0000u, 0x8000u, "ROM", true, false},
    {PluginCategory::System, 0xC000u, 0x2000u, "RAM", true, true},
    {PluginCategory::Audio, 0xFF10u, 0x0017u, "APU Registers", true, true},
    {PluginCategory::Video, 0x00BEu, 0x0002u, "VDP Ports (IO)", true, true},
    {PluginCategory::DigitalInput, 0x00DCu, 0x0002u, "Input Ports (0xDC/0xDD)", true, false},
}};

[[nodiscard]] bool romPathAllowsSaveBinding(const std::optional<std::filesystem::path>& path)
{
    if (!path.has_value()) {
        return false;
    }
    auto extension = path->extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension != ".sms";
}

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
    std::unique_ptr<GameGearMapper> cart;
    GameGearMemoryMap mem;
    PluginManager pluginManager;
    GameGearSaveManager saveManager;
    Plugin::DefaultStepPolicy defaultPolicy;
    Plugin::IExecutorPolicyPlugin* activePolicy = &defaultPolicy;
    bool romLoaded = false;
    std::optional<std::filesystem::path> pendingRomSourcePath;
    std::optional<uint32_t> lastDigitalInputMask;
    uint64_t inputGeneration = 0u;
    uint64_t stepCounter = 0u;
    uint64_t lastAudioFrameCounter = 0u;
    // Interrupt request raised by VDP (VBlank) or other devices. Consumed
    // atomically by the Z80 interrupt provider.
    bool interruptRequested = false;
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
    (void)audioService().configureEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 2u,
        .ringBufferCapacitySamples = 4096u,
        .frameChunkSamples = 512u,
    });
    // Wire up CPU memory interface to memory map
    impl->cpu.setMemoryInterface(
        [this](uint16_t addr) { return impl->mem.read(addr); },
        [this](uint16_t addr, uint8_t val) { impl->mem.write(addr, val); }
    );
    impl->cpu.setIoInterface(
        [this](uint8_t port) { return impl->mem.readIoPort(port); },
        [this](uint8_t port, uint8_t value) { impl->mem.writeIoPort(port, value); }
    );
    // Leave memory map using its fallback cartridge until a ROM is loaded.
    impl->mem.setInput(&impl->input);
    impl->mem.setPsg(&impl->psg);
    impl->mem.setVdp(&impl->vdp);
    // Provide a callback for the CPU to atomically consume pending IRQs.
    // Return an optional data byte: present => interrupt pending; empty => none.
    impl->cpu.setInterruptRequestProvider([this]() -> std::optional<uint8_t> {
        if (impl->interruptRequested) {
            impl->interruptRequested = false;
            // No device-supplied vector byte available; return 0 as a
            // harmless placeholder. IM1 ignores the byte.
            return static_cast<uint8_t>(0u);
        }
        return std::nullopt;
    });
    Plugin::validateExecutorPolicyStartup(impl->defaultPolicy);
    impl->vdp.reset();
    impl->input.reset();
    impl->cpu.reset();
}
GameGearMachine::~GameGearMachine() {
    (void)flushCartridgeSave();
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
    (void)flushCartridgeSave();
    // Create an appropriate mapper for the ROM and bind it into the
    // memory map. The factory returns a mapper with the ROM already
    // loaded.
    impl->cart = createMapperFromRom(bytes.data(), bytes.size(), impl->pendingRomSourcePath);
    if (!impl->cart) {
        throw std::runtime_error("Failed to create mapper for ROM");
    }
    impl->mem.setCartridge(impl->cart.get());

    if (romPathAllowsSaveBinding(impl->pendingRomSourcePath)) {
        impl->saveManager.bindRomPath(*impl->pendingRomSourcePath);
        impl->saveManager.load(*impl->cart);
    } else {
        impl->saveManager.clearBinding();
    }
    impl->pendingRomSourcePath.reset();
    impl->mem.reset();
    impl->vdp.reset();
    impl->psg.reset();
    impl->input.reset();
    impl->romLoaded = true;
    ++impl->inputGeneration;
    inputService().advanceGeneration(impl->inputGeneration);
    impl->lastDigitalInputMask.reset();
    impl->stepCounter = 0u;
    impl->lastAudioFrameCounter = 0u;
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

void GameGearMachine::setRomSourcePath(const std::optional<std::filesystem::path>& path) {
    impl->pendingRomSourcePath = path;
}

void GameGearMachine::loadExternalBootRom(const std::vector<uint8_t>& bytes) {
    impl->mem.mapBios(bytes.data(), bytes.size());
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
    const auto feedback = impl->context.step();
    ++impl->stepCounter;
    impl->vdp.step(feedback.retiredCycles);
    impl->psg.step(feedback.retiredCycles);
    if (impl->vdp.takeScanlineReady() && impl->pluginManager.size() != 0u) {
        const auto vdpScanline = impl->vdp.lastReadyScanline();
        if (vdpScanline >= kGameGearViewportY &&
            vdpScanline < static_cast<uint8_t>(kGameGearViewportY + kGameGearViewportHeight)) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::VideoScanlineReady,
                PluginCategory::Video,
                impl->stepCounter,
                0, // No memory-mapped address for scanline
                static_cast<uint8_t>(vdpScanline - kGameGearViewportY),
                &runtimeContext().getLastFeedback(),
                "scanline ready"
            });
        }
    }
    if (impl->vdp.takeVBlankEntered()) {
        if (impl->pluginManager.size() != 0u) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::VBlank,
                PluginCategory::Video,
                impl->stepCounter,
                0, // No memory-mapped address for VBlank
                impl->vdp.currentScanline(),
                &runtimeContext().getLastFeedback(),
                "entered VBlank"
            });
        }
    }
    if (impl->vdp.takeIrqAsserted()) {
        impl->interruptRequested = true;
    }
    const auto audioFrameCounter = impl->psg.frameCounter();
    if (audioFrameCounter != impl->lastAudioFrameCounter) {
        impl->lastAudioFrameCounter = audioFrameCounter;
        if (impl->pluginManager.size() != 0u) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::AudioFrameReady,
                PluginCategory::Audio,
                impl->stepCounter,
                0xFF26u,
                runtimeContext().read8(0xFF26u),
                &runtimeContext().getLastFeedback(),
                "psg frame mixed"
            });
        }
    }
}

void GameGearMachine::serviceInput() {
    if (inputService().state() == InputLifecycleState::Active) {
        (void)inputService().pollActiveAdapter(impl->inputGeneration);
        if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
            const auto pressedMask = *committedInput;
            impl->input.setLogicalButtons(pressedMask);
            impl->lastDigitalInputMask = pressedMask;
            return;
        }
    }

    if (impl->pluginManager.size() == 0u) {
        return;
    }

    if (const auto sampledInput = impl->pluginManager.sampleDigitalInput(view()); sampledInput.has_value()) {
        const auto pressedMask = *sampledInput;
        impl->input.setLogicalButtons(pressedMask);
        impl->lastDigitalInputMask = pressedMask;
    }
}

std::optional<uint32_t> GameGearMachine::currentDigitalInputMask() const {
    return impl->lastDigitalInputMask;
}

std::vector<int16_t> GameGearMachine::recentAudioSamples() const {
    return impl->psg.copyRecentSamples();
}

uint32_t GameGearMachine::audioSampleRate() const {
    return impl->psg.sampleRate();
}

uint8_t GameGearMachine::audioChannelCount() const {
    return impl->psg.outputChannelCount();
}

uint64_t GameGearMachine::audioFrameCounter() const {
    return impl->psg.frameCounter();
}

std::optional<VideoDebugFrameModel> GameGearMachine::videoDebugFrameModel(
    const VideoDebugRenderRequest& request) const
{
    return impl->vdp.buildFrameModel(request);
}

std::optional<RealtimeVideoPacket> GameGearMachine::realtimeVideoPacket(
    const VideoDebugRenderRequest& request) const
{
    return impl->vdp.buildRealtimeFrame(request);
}

uint32_t GameGearMachine::clockHz() const {
    return impl->context.clockHz();
}

std::string GameGearMachine::stopSummary() const {
    std::ostringstream out;
    out << "PC=0x" << std::hex << std::uppercase << impl->cpu.PC
        << " SP=0x" << impl->cpu.SP
        << " AF=0x" << impl->cpu.AF
        << " LY=0x" << static_cast<int>(impl->vdp.currentScanline())
        << std::dec << '\n'
        << "Input port=0x" << std::hex << std::uppercase << static_cast<int>(impl->input.readInputs())
        << std::dec << '\n'
        << "MEM=0x" << std::hex << std::uppercase << static_cast<int>(impl->mem.memoryControlValue())
        << " IO=0x" << static_cast<int>(impl->mem.ioControlValue())
        << " BIOS=" << (impl->mem.hasBios() ? "Yes" : "No") << std::dec << '\n';

    if (impl->cart) {
        if (auto* concrete = dynamic_cast<GameGearCartridge*>(impl->cart.get())) {
            const auto banks = concrete->bankRegisters();
            out << "Cart CTRL=0x" << std::hex << std::uppercase << static_cast<int>(concrete->controlRegister())
                << " banks=[" << std::dec << static_cast<int>(banks[0]) << "," << static_cast<int>(banks[1]) << "," << static_cast<int>(banks[2]) << "]\n";
        }
    }
    return out.str();
}

bool GameGearMachine::flushCartridgeSave() {
    if (!impl->cart) return false;
    return impl->saveManager.flush(*impl->cart);
}

bool GameGearMachine::cpuInterruptsEnabled() const {
    return impl->cpu.IME;
}

} // namespace BMMQ
