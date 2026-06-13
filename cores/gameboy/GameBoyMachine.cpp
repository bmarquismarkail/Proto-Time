#include "GameBoyMachine.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "../../inst_cycle/executor/PluginContract.hpp"
#include "../../machine/plugins/IoPlugin.hpp"
#include "../../machine/plugins/PluginManager.hpp"
#include "../../machine/BackgroundTaskService.hpp"
#include "cartridge/CartridgeSaveManager.hpp"
#include "hardware_registers.hpp"
#include "register_id.hpp"
#include "video/GameBoyVisualDebugAdapter.hpp"

namespace GB {

// Expose Game Boy memory-mapped I/O descriptors.
static constexpr std::size_t kMaxRomSize = 1024u * 1024u;

constexpr std::array<BMMQ::IoRegionDescriptor, 7> kIoRegions{{
    {BMMQ::PluginCategory::Video, 0x8000u, 0x2000u, "VRAM", true, true},
    {BMMQ::PluginCategory::Video, 0xFE00u, 0x00A0u, "OAM", true, true},
    {BMMQ::PluginCategory::Video, 0xFF40u, 0x000Cu, "LCD Registers", true, true},
    {BMMQ::PluginCategory::Audio, 0xFF10u, 0x0017u, "APU Registers", true, true},
    {BMMQ::PluginCategory::Audio, 0xFF30u, 0x0010u, "Wave RAM", true, true},
    {BMMQ::PluginCategory::DigitalInput, 0xFF00u, 0x0001u, "Joypad", true, true},
    {BMMQ::PluginCategory::Serial, 0xFF01u, 0x0002u, "Serial Registers", true, true},
}};

[[nodiscard]] inline bool romPathAllowsSaveBinding(const std::optional<std::filesystem::path>& path) {
    if (!path.has_value()) return false;
    auto extension = path->extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension != ".sms"; // Game Boy ROMs allow saves, SMS typically doesn't
}

inline void flushSaveSnapshotViaBackground(
    BMMQ::BackgroundTaskService& backgroundTaskService,
    CartridgeSaveManager::SaveSnapshot snapshot)
{
    auto queuedSnapshot = snapshot;
    const bool queued = backgroundTaskService.submit([snapshot = std::move(queuedSnapshot)]() {
        CartridgeSaveManager::flushSnapshot(snapshot);
    });
    if (!queued) {
        CartridgeSaveManager::flushSnapshot(snapshot);
    }
}

// Game Boy runtime context — mirrors GameGearRuntimeContext pattern.
class GameBoyRuntimeContext final : public BMMQ::RuntimeContext {
public:
    GameBoyRuntimeContext(LR3592_PluginRuntime& runtime,
                          GameBoyMemoryMap& memoryMap,
                          bool& romLoaded,
                          BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy)
        : runtime_(runtime),
          memoryMap_(memoryMap),
          romLoaded_(romLoaded),
          activePolicy_(activePolicy) {
        refreshExecutionMode();
    }

    FetchBlock fetch() override {
        if (!romLoaded_) {
            throw std::runtime_error("ROM is not loaded");
        }
        return runtime_.fetch();
    }

    ExecutionBlock decode(FetchBlock& fetchBlock) override {
        return runtime_.decode(fetchBlock);
    }

    void execute(const ExecutionBlock& block, FetchBlock& fetchBlock) override {
        runtime_.execute(block, fetchBlock);
    }

    BMMQ::CpuFeedback step(FetchBlock& fetchBlock) override {
        if (fastExecutionAllowed() && runtime_.cpu().tryFastExecute(fetchBlock)) {
            return runtime_.getLastFeedback();
        }
        cachedExecutionBlock_.clear();
        cachedExecutionBlock_.reserve(4);
        runtime_.cpu().decodeInto(fetchBlock, cachedExecutionBlock_);
        runtime_.execute(cachedExecutionBlock_, fetchBlock);
        return runtime_.getLastFeedback();
    }

    BMMQ::CpuFeedback step() override {
        if (!romLoaded_) {
            throw std::runtime_error("ROM is not loaded");
        }
        runtime_.cpu().fetchInto(cachedFetchBlock_);
        return step(cachedFetchBlock_);
    }

    uint8_t read8(uint16_t address) const override {
        return memoryMap_.read(resolveEchoAddress(address));
    }

    uint8_t peek8(uint16_t address) const override {
        address = resolveEchoAddress(address);
        // For addresses that go through cartridge intercept, use read8
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {
            return read8(address);
        }
        return memoryMap_.read(address);
    }

    void write8(uint16_t address, uint8_t value) override {
        memoryMap_.write(resolveEchoAddress(address), value);
    }

    uint8_t readRegister8(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            return read8(*descriptor.mappedAddress);
        }
        auto* entry = requireRegisterEntry(id);
        return static_cast<uint8_t>(entry->reg->value & 0x00FFu);
    }

    void writeRegister8(std::string_view id, uint8_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            write8(*descriptor.mappedAddress, value);
            return;
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    uint16_t readRegister16(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            return read16(*descriptor.mappedAddress);
        }
        auto* entry = requireRegisterEntry(id);
        return entry->reg->value;
    }

    void writeRegister16(std::string_view id, uint16_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            write16(*descriptor.mappedAddress, value);
            return;
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    uint16_t readRegisterPair(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (!descriptor.isPair) {
            throw std::invalid_argument("register is not a pair");
        }
        auto* entry = requireRegisterEntry(id);
        return entry->reg->value;
    }

    void writeRegisterPair(std::string_view id, uint16_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (!descriptor.isPair) {
            throw std::invalid_argument("register is not a pair");
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    const BMMQ::CpuFeedback& getLastFeedback() const override {
        return runtime_.getLastFeedback();
    }

    uint32_t clockHz() const override {
        return runtime_.cpu().clockHz();
    }

    BMMQ::ExecutionGuarantee guarantee() const override {
        return activePolicy_->guarantee();
    }

    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override {
        return &activePolicy_->metadata();
    }

    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

    void refreshExecutionMode() {
        allowFastPath_ = activePolicy_ != nullptr &&
            activePolicy_->guarantee() != BMMQ::ExecutionGuarantee::BaselineFaithful;
    }

    [[nodiscard]] bool fastExecutionAllowed() const noexcept {
        return allowFastPath_;
    }

private:
    static uint16_t resolveEchoAddress(uint16_t address) {
        if (address >= 0xE000 && address <= 0xFDFF) {
            return static_cast<uint16_t>(address - 0x2000);
        }
        return address;
    }

    using RegisterEntry = decltype(std::declval<BMMQ::RegisterFile<uint16_t>&>().findRegister("AF"));

    const BMMQ::RegisterDescriptor& requireDescriptor(std::string_view id) const {
        const auto* descriptor = runtime_.cpu().getMemory().file.findDescriptor(id);
        if (descriptor == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return *descriptor;
    }

    RegisterEntry requireRegisterEntry(std::string_view id) const {
        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return entry;
    }

    LR3592_PluginRuntime& runtime_;
    GameBoyMemoryMap& memoryMap_;
    FetchBlock cachedFetchBlock_{};
    ExecutionBlock cachedExecutionBlock_{};
    const bool& romLoaded_;
    BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy_;
    bool allowFastPath_ = false;
};


// ---------------------------------------------------------------------------
// GameBoyMachine implementation
// ---------------------------------------------------------------------------
GameBoyMachine::GameBoyMachine() : impl_(std::make_unique<Impl>()) {
    videoService().setVisualDebugAdapter(visualDebugAdapter());

    // Wire CPU memory interface to memory map
    impl_->cpu.cpu().attachMemory(impl_->memoryMap);

    // Wire PPU to memory map for VRAM/OAM reads
    impl_->ppu.memoryMap = &impl_->memoryMap;

    // Set up mapper reference in memory map
    impl_->memoryMap.setMapper(&impl_->mapper);
    impl_->memoryMap.setCartridge(&impl_->cartridge_);
    impl_->memoryMap.setWriteObserver([this](uint16_t address, uint8_t value) {
        impl_->cpu.cpu().syncCachedIoRegisterWrite(address, value);
        if (address == 0xFF00u) {
            impl_->input.writeRegister(value);
            impl_->memoryMap.setIoRegisterRaw(0xFF00u, impl_->input.readRegister());
        }
        if ((address >= 0xFF10u && address <= 0xFF26u) ||
            (address >= 0xFF30u && address <= 0xFF3Fu)) {
            impl_->apu.writeRegister(address, value);
            impl_->memoryMap.setIoRegisterRaw(0xFF26u, impl_->apu.readRegister(0xFF26u));
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::MemoryWriteObserved,
                    BMMQ::PluginCategory::Audio,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "audio register write"
                });
            }
        }
        if (address == 0xFF50u && value != 0) {
            impl_->bootEntryPending = true;
        }
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {
            impl_->cartridge_.write(address, value);
        }
        if (address < 0x8000u) {
            impl_->cpu.cpu().invalidateAllBlockCache();
        }
        if (address == 0xFF40u) {
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::MemoryWriteObserved,
                    BMMQ::PluginCategory::Video,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "video memory write"
                });
            }
        }
        if (address == 0xFF01u || address == 0xFF02u) {
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::SerialActivity,
                    BMMQ::PluginCategory::Serial,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "serial register write"
                });
            }
        }
    });

    // Create runtime context
    impl_->context = std::make_unique<GameBoyRuntimeContext>(
        impl_->cpu,
        impl_->memoryMap,
        impl_->romLoaded,
        impl_->activePolicy);

    // Configure audio engine
    (void)audioService().configureEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 1u,
        .ringBufferCapacitySamples = 2048u,
        .frameChunkSamples = 256u,
    });

    // Leave memory map using its fallback cartridge until a ROM is loaded
    impl_->input.reset();
    impl_->ppu.reset();
    impl_->apu.reset();
    impl_->cpu.cpu().resetDivider();
    impl_->cpu.cpu().setJoypadState(0x00u);

    BMMQ::Plugin::validateExecutorPolicyStartup(impl_->defaultPolicy);
}

GameBoyMachine::~GameBoyMachine() {
    (void)flushCartridgeSave();
    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.shutdown(view());
    }
}

void GameBoyMachine::loadRom(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot load empty ROM");
    }
    if (bytes.size() > kMaxRomSize) {
        throw std::runtime_error("ROM too large");
    }

    (void)flushCartridgeSave();

    // Load into cartridge (for save management)
    impl_->cartridge_.load(bytes);

    // Create mapper from ROM data
    impl_->mapper.load(bytes);

    // Bind save path if applicable
    if (romPathAllowsSaveBinding(impl_->pendingRomSourcePath)) {
        impl_->saveManager.bindRomPath(*impl_->pendingRomSourcePath);
        impl_->saveManager.load(impl_->cartridge_);
    } else {
        impl_->saveManager.clearBinding();
    }
    impl_->pendingRomSourcePath.reset();

    // Reset subsystems
    impl_->memoryMap.reset();
    impl_->ppu.reset();
    impl_->apu.reset();
    impl_->input.reset();
    impl_->cpu.cpu().resetDivider();
    impl_->cpu.cpu().setJoypadState(0x00u);

    impl_->romLoaded = true;
    ++impl_->inputGeneration;
    inputService().advanceGeneration(impl_->inputGeneration);
    impl_->lastDigitalInputMask.reset();
    impl_->stepCounter = 0u;
    impl_->lastAudioFrameCounter = 0u;
    impl_->bootEntryPending = false;

    std::array<uint8_t, 0x4000> fixedWindow{};
    impl_->mapper.copyRomBankWindow(0, fixedWindow);
    impl_->memoryMap.installRomWindow(0x0000u, fixedWindow);
    std::array<uint8_t, 0x4000> switchableWindow{};
    impl_->mapper.copyRomBankWindow(impl_->mapper.currentRomBank(), switchableWindow);
    impl_->memoryMap.installRomWindow(0x4000u, switchableWindow);

    // Initialize DMG startup registers
    auto& core = impl_->cpu.cpu();
    core.setIme(false);
    core.setStopFlag(false);
    core.clearHaltFlag();
    core.resetDivider();
    core.setJoypadState(0x00u);

    // Set CPU registers per Pan Docs
    impl_->context->writeRegister16(GB::RegisterId::AF, 0x01B0);
    impl_->context->writeRegister16(GB::RegisterId::BC, 0x0013);
    impl_->context->writeRegister16(GB::RegisterId::DE, 0x00D8);
    impl_->context->writeRegister16(GB::RegisterId::HL, 0x014D);
    impl_->context->writeRegister16(GB::RegisterId::SP, 0xFFFE);
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0100);

    impl_->context->write8(0xFF00, 0xCF);
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x01u);

    // Seed I/O register defaults
    static constexpr std::array<std::pair<std::string_view, uint8_t>, 37> kPostBootIoDefaults{{
        {"SB", 0x00}, {"SC", 0x7E}, {"TIMA", 0x00}, {"TMA", 0x00}, {"TAC", 0xF8},
        {"IF", 0xE1}, {"NR10", 0x80}, {"NR11", 0xBF}, {"NR12", 0xF3}, {"NR14", 0xBF},
        {"NR21", 0x3F}, {"NR22", 0x00}, {"NR24", 0xBF}, {"NR30", 0x7F}, {"NR31", 0xFF},
        {"NR32", 0x9F}, {"NR34", 0xBF}, {"NR41", 0xFF}, {"NR42", 0x00}, {"NR43", 0x00},
        {"NR44", 0xBF}, {"NR50", 0x77}, {"NR51", 0xF3}, {"NR52", 0xF1},
        {"LCDC", 0x91}, {"STAT", 0x85}, {"SCY", 0x00}, {"SCX", 0x00},
        {"LY", 0x00}, {"LYC", 0x00}, {"DMA", 0xFF},
        {"BGP", 0xFC}, {"OBP0", 0xFF}, {"OBP1", 0xFF},
        {"WY", 0x00}, {"WX", 0x00}, {"IE", 0x00},
    }};

    for (const auto& [name, value] : kPostBootIoDefaults) {
        auto* entry = impl_->cpu.cpu().getMemory().file.findRegister(name);
        if (entry != nullptr && entry->reg != nullptr) {
            entry->reg->value = value;
        }
        auto* desc = impl_->cpu.cpu().getMemory().file.findDescriptor(name);
        if (desc != nullptr && desc->mappedAddress.has_value()) {
            impl_->memoryMap.write(*desc->mappedAddress, value);
        }
    }

    core.resetApu();
    impl_->lastAudioFrameCounter = core.audioFrameCounter();

    // Emit RomLoaded event
    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::RomLoaded,
            BMMQ::PluginCategory::System,
            0u,
            0u,
            0u,
            nullptr,
            "ROM loaded"
        });
    }
}

void GameBoyMachine::loadRomFromPath(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open ROM: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        throw std::runtime_error("ROM is empty: " + path.string());
    }

    setRomSourcePath(path);
    loadRom(bytes);
}

void GameBoyMachine::loadExternalBootRom(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != 0x100u) {
        throw std::invalid_argument("Game Boy boot ROM must be exactly 256 bytes");
    }
    impl_->memoryMap.mapBootRom(bytes.data(), bytes.size());
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x00u);
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0000u);
}

void GameBoyMachine::setRomSourcePath(const std::optional<std::filesystem::path>& path) {
    impl_->pendingRomSourcePath = path;
}

BMMQ::RuntimeContext& GameBoyMachine::runtimeContext() {
    return *impl_->context;
}

const BMMQ::RuntimeContext& GameBoyMachine::runtimeContext() const {
    return *impl_->context;
}

BMMQ::PluginManager& GameBoyMachine::pluginManager() {
    return impl_->pluginManager;
}

const BMMQ::PluginManager& GameBoyMachine::pluginManager() const {
    return impl_->pluginManager;
}

std::span<const BMMQ::IoRegionDescriptor> GameBoyMachine::describeIoRegions() const {
    return kIoRegions;
}

void GameBoyMachine::attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) {
    BMMQ::Plugin::validateExecutorPolicyStartup(policy);
    impl_->activePolicy = &policy;
    impl_->context->refreshExecutionMode();
}

const BMMQ::Plugin::IExecutorPolicyPlugin& GameBoyMachine::attachedExecutorPolicy() const {
    return *impl_->activePolicy;
}

void GameBoyMachine::step() {
    if (impl_->bootEntryPending) {
        impl_->bootEntryPending = false;
        impl_->context->writeRegister16(GB::RegisterId::PC, 0x0100u);
    }
    // Handle boot entry pending (FF50 write during boot ROM)
    // In the new architecture, boot ROM is handled by memory map intercept
    // The CPU's handleMemoryWrite will catch FF50 writes

    auto fetchBlock = impl_->context->fetch();
    if (!impl_->context->fastExecutionAllowed() ||
        !impl_->cpu.cpu().tryExecuteFromCache(fetchBlock)) {
        impl_->context->step(fetchBlock);
        if (impl_->context->fastExecutionAllowed()) {
            impl_->cpu.cpu().populateBlockCache(fetchBlock);
        }
    }
    ++impl_->stepCounter;
    const auto& feedback = impl_->context->getLastFeedback();

    // Advance PPU by retired cycles
    impl_->ppu.step(feedback.retiredCycles);
    impl_->memoryMap.setIoRegisterRaw(0xFF44u, impl_->ppu.ly());
    impl_->memoryMap.setIoRegisterRaw(0xFF41u, static_cast<uint8_t>(
        (impl_->context->read8(0xFF41u) & 0xF8u) | (impl_->ppu.currentMode() & 0x03u)));

    // Advance APU by retired cycles
    impl_->apu.step(feedback.retiredCycles);
    impl_->memoryMap.setIoRegisterRaw(0xFF26u, impl_->apu.readRegister(0xFF26u));

    // Check PPU scanline ready events
    if (impl_->ppu.takeScanlineReady()) {
        auto ly = impl_->ppu.lastReadyScanline();
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::VideoScanlineReady,
                BMMQ::PluginCategory::Video,
                impl_->stepCounter,
                0xFF44u,
                ly,
                &feedback,
                "visible scanline ready"
            });
        }
    }

    // Check VBlank events
    if (impl_->ppu.takeVBlankEntered()) {
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::VBlank,
                BMMQ::PluginCategory::Video,
                impl_->stepCounter,
                0xFF44u,
                static_cast<uint8_t>(144u),
                &feedback,
                "entered VBlank"
            });
        }
        // Flush cartridge save on VBlank
        if (impl_->backgroundTaskService == nullptr) {
            (void)flushCartridgeSave();
        } else {
            auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
            if (extracted.has_value()) {
                flushSaveSnapshotViaBackground(*impl_->backgroundTaskService, std::move(*extracted));
            }
        }
    }

    // Check audio frame counter changes
    const auto audioFrameCounter = impl_->apu.frameCounter();
    if (audioFrameCounter != impl_->lastAudioFrameCounter) {
        impl_->lastAudioFrameCounter = audioFrameCounter;
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::AudioFrameReady,
                BMMQ::PluginCategory::Audio,
                impl_->stepCounter,
                0xFF26u,
                impl_->context->read8(0xFF26u),
                &feedback,
                "apu frame mixed"
            });
        }
    }

    // Periodic save flush
    if ((impl_->stepCounter % 4096u) == 0u) {
        if (impl_->backgroundTaskService != nullptr) {
            auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
            if (extracted.has_value()) {
                flushSaveSnapshotViaBackground(*impl_->backgroundTaskService, std::move(*extracted));
            }
        }
    }
}

void GameBoyMachine::serviceInput() {
    if (inputService().state() == BMMQ::InputLifecycleState::Active) {
        (void)inputService().pollActiveAdapter(impl_->inputGeneration);
        if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
            const auto pressedMask = static_cast<uint8_t>(*committedInput & 0x00FFu);
            impl_->input.setLogicalButtons(pressedMask);
            impl_->lastDigitalInputMask = pressedMask;
            const auto joypad = impl_->input.readRegister();
            impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF00u, joypad);
            impl_->memoryMap.setIoRegisterRaw(0xFF00u, joypad);
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::DigitalInputChanged,
                    BMMQ::PluginCategory::DigitalInput,
                    impl_->stepCounter,
                    0xFF00u,
                    joypad,
                    nullptr,
                    "input service sample"
                });
            }
            return;
        }
    }

    if (!impl_->pluginManager.initialized()) {
        return;
    }

    if (const auto sampledInput = impl_->pluginManager.sampleDigitalInput(view()); sampledInput.has_value()) {
        const auto pressedMask = static_cast<uint8_t>(*sampledInput & 0x00FFu);
        impl_->input.setLogicalButtons(pressedMask);
        impl_->lastDigitalInputMask = pressedMask;
        const auto joypad = impl_->input.readRegister();
        impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF00u, joypad);
        impl_->memoryMap.setIoRegisterRaw(0xFF00u, joypad);
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::DigitalInputChanged,
            BMMQ::PluginCategory::DigitalInput,
            impl_->stepCounter,
            0xFF00u,
            joypad,
            nullptr,
            "plugin input sample"
        });
    }
}

std::optional<uint32_t> GameBoyMachine::currentDigitalInputMask() const {
    if (!impl_->lastDigitalInputMask.has_value()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*impl_->lastDigitalInputMask);
}

std::vector<int16_t> GameBoyMachine::recentAudioSamples() const {
    return impl_->apu.copyRecentSamples();
}

uint32_t GameBoyMachine::audioSampleRate() const {
    return impl_->apu.sampleRate();
}

uint8_t GameBoyMachine::audioChannelCount() const {
    return 1u;
}

uint64_t GameBoyMachine::audioFrameCounter() const {
    return impl_->apu.frameCounter();
}

std::string_view GameBoyMachine::visualTargetId() const noexcept {
    return "gameboy";
}

const BMMQ::IVisualDebugAdapter* GameBoyMachine::visualDebugAdapter() const noexcept {
    return &gameBoyVisualDebugAdapter();
}

std::optional<BMMQ::VideoDebugFrameModel> GameBoyMachine::videoDebugFrameModel(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    return gameBoyVisualDebugAdapter().buildFrameModel(*this, request);
}

std::optional<BMMQ::RealtimeVideoPacket> GameBoyMachine::realtimeVideoPacket(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    return impl_->ppu.buildRealtimeFrame(request);
}

std::optional<BMMQ::RealtimeAudioPacket> GameBoyMachine::realtimeAudioPacket() const {
    BMMQ::RealtimeAudioPacket packet;
    packet.sampleRate = impl_->apu.sampleRate();
    packet.channelCount = 1u;
    packet.frameCounter = impl_->apu.frameCounter();
    packet.pcmSamples = impl_->apu.takePendingSamples();
    return packet;
}

bool GameBoyMachine::flushCartridgeSave() {
    if (!impl_->cartridge_.supportsBatterySave()) return false;
    auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
    if (!extracted.has_value()) return false;
    GB::CartridgeSaveManager::flushSnapshot(std::move(*extracted));
    return true;
}

uint16_t GameBoyMachine::readRegisterPair(std::string_view id) const {
    return impl_->context->readRegister16(id);
}

std::string GameBoyMachine::stopSummary() const {
    const auto pc = impl_->context->readRegister16(GB::RegisterId::PC);
    const auto ly = impl_->context->read8(0xFF44);
    const auto lcdc = impl_->context->read8(0xFF40);
    const auto stat = impl_->context->read8(0xFF41);
    const auto interruptFlags = impl_->context->read8(0xFF0F);
    const auto interruptEnable = impl_->context->read8(0xFFFF);

    std::ostringstream out;
    out << "PC=0x" << std::hex << std::uppercase << pc << std::dec << '\n'
        << "I/O state: LY=0x" << std::hex << std::uppercase << static_cast<int>(ly)
        << " LCDC=0x" << static_cast<int>(lcdc)
        << " STAT=0x" << static_cast<int>(stat)
        << " IF=0x" << static_cast<int>(interruptFlags)
        << " IE=0x" << static_cast<int>(interruptEnable)
        << std::dec;
    return out.str();
}

void GameBoyMachine::setJoypadState(uint8_t value) {
    impl_->cpu.cpu().setJoypadState(value);
    impl_->input.setLogicalButtons(value);
    impl_->lastDigitalInputMask = value;
    impl_->memoryMap.write(0xFF00u, impl_->input.readRegister());

    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::DigitalInputChanged,
            BMMQ::PluginCategory::DigitalInput,
            impl_->stepCounter,
            0xFF00u,
            impl_->input.readRegister(),
            nullptr,
            "joypad state changed"
        });
    }
}

GameBoyMachine::BlockCacheStats GameBoyMachine::blockCacheStats() const {
    const auto stats = impl_->cpu.cpu().blockCacheStats();
    return BlockCacheStats{
        stats.hits.load(std::memory_order_relaxed),
        stats.misses.load(std::memory_order_relaxed),
        stats.invalidations.load(std::memory_order_relaxed)
    };
}

bool GameBoyMachine::blockCacheEnabled() const {
    return impl_->cpu.cpu().blockCacheEnabled();
}

void GameBoyMachine::setBlockCacheEnabled(bool enabled) {
    impl_->cpu.cpu().setBlockCacheEnabled(enabled);
}

} // namespace GB
