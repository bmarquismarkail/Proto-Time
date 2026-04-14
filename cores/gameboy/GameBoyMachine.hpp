#ifndef GAMEBOY_MACHINE_HPP
#define GAMEBOY_MACHINE_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <vector>

#include "../../machine/Machine.hpp"
#include "../../machine/MemoryMap.hpp"
#include "../../machine/RegisterId.hpp"
#include "../../machine/RomImage.hpp"
#include "../../machine/RuntimeContext.hpp"
#include "../../machine/plugins/PluginManager.hpp"
#include "register_id.hpp"
#include "hardware_registers.hpp"
#include "gameboy_plugin_runtime.hpp"

class GameBoyRuntimeContext final : public BMMQ::RuntimeContext {
public:
    GameBoyRuntimeContext(
        LR3592_PluginRuntime& runtime,
        BMMQ::MemoryMap& memoryMap,
        const bool& romLoaded,
        BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy)
        : runtime_(runtime), memoryMap_(memoryMap), romLoaded_(romLoaded), activePolicy_(activePolicy) {
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
        if (allowFastPath_ && runtime_.cpu().tryFastExecute(fetchBlock)) {
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

    DataType read8(AddressType address) const override {
        return memoryMap_.read8(resolveEchoAddress(address));
    }

    DataType peek8(AddressType address) const override {
        address = resolveEchoAddress(address);
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {
            return read8(address);
        }
        return memoryMap_.storage().readableSpan(address, 1)[0];
    }

    void write8(AddressType address, DataType value) override {
        memoryMap_.write8(resolveEchoAddress(address), value);
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

private:
    static AddressType resolveEchoAddress(AddressType address) {
        if (address >= 0xE000 && address <= 0xFDFF) {
            return static_cast<AddressType>(address - 0x2000);
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
    BMMQ::MemoryMap& memoryMap_;
    FetchBlock cachedFetchBlock_{};
    ExecutionBlock cachedExecutionBlock_{};
    const bool& romLoaded_;
    BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy_;
    bool allowFastPath_ = false;
};

class GameBoyMachine final : public BMMQ::Machine {
public:
    GameBoyMachine()
        : activePolicy_(&defaultPolicy_),
          context_(cpu_, memoryMap_, romLoaded_, activePolicy_) {
        BMMQ::Plugin::validateRuntimeStartup(cpu_);
        BMMQ::Plugin::validateExecutorPolicyStartup(defaultPolicy_);
        configureMemoryMap();
        cpu_.attachMemory(memoryMap_.storage());
    }

    ~GameBoyMachine() override {
        if (pluginManager_.initialized()) {
            pluginManager_.shutdown(view());
        }
    }

    BMMQ::PluginManager& pluginManager() override {
        return pluginManager_;
    }

    const BMMQ::PluginManager& pluginManager() const override {
        return pluginManager_;
    }

    std::span<const BMMQ::IoRegionDescriptor> describeIoRegions() const override {
        return kIoRegionDescriptors;
    }

    void loadRom(const std::vector<uint8_t>& bytes) override {
        if (bytes.empty()) {
            throw std::invalid_argument("ROM must not be empty");
        }

        bootEntryPending_ = false;
        rom_.load(bytes);
        configureCartridge(bytes);
        configureMemoryMap();
        installVisibleRomBanks();
        initializeDmgStartupRegisters();
        romLoaded_ = true;
        ++inputGeneration_;
        inputService().advanceGeneration(inputGeneration_);
        lastDigitalInputMask_.reset();
        lastPolledDigitalInput_.reset();
        lastLy_ = context_.read8(0xFF44);
        emitMachineEvent(BMMQ::MachineEvent{
            BMMQ::MachineEventType::RomLoaded,
            BMMQ::PluginCategory::System,
            stepCounter_,
            0,
            0,
            nullptr,
            "ROM loaded"
        });
    }

    void loadBootRom(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != bootRom_.size()) {
            throw std::invalid_argument("Game Boy boot ROM must be exactly 256 bytes");
        }

        std::copy(bytes.begin(), bytes.end(), bootRom_.begin());
        bootRomMapped_ = true;
        context_.writeRegister16(GB::RegisterId::PC, 0x0000);
        bootEntryPending_ = false;
        const uint8_t bootControl = 0x00u;
        memoryMap_.storage().load(std::span<const uint8_t>(&bootControl, 1), 0xFF50);
    }

    void setJoypadState(uint8_t pressedMask) {
        lastDigitalInputMask_ = pressedMask;
        lastPolledDigitalInput_ = pressedMask;
        cpu_.cpu().setJoypadState(pressedMask);
        emitMachineEvent(BMMQ::MachineEvent{
            BMMQ::MachineEventType::DigitalInputChanged,
            BMMQ::PluginCategory::DigitalInput,
            stepCounter_,
            0xFF00,
            pressedMask,
            nullptr,
            "joypad state updated"
        });
    }

    void step() override {
        pollInputPlugins();
        if (bootEntryPending_) {
            context_.writeRegister16(GB::RegisterId::PC, 0x0100);
            bootEntryPending_ = false;
            return;
        }
        context_.step();
        ++stepCounter_;
        const auto& feedback = context_.getLastFeedback();
        emitMachineEvent(BMMQ::MachineEvent{
            BMMQ::MachineEventType::StepCompleted,
            BMMQ::PluginCategory::System,
            stepCounter_,
            0,
            0,
            &feedback,
            "instruction step completed"
        });

        const auto ly = context_.read8(0xFF44);
        if (lastLy_ < 144u && ly >= 144u) {
            emitMachineEvent(BMMQ::MachineEvent{
                BMMQ::MachineEventType::VBlank,
                BMMQ::PluginCategory::Video,
                stepCounter_,
                0xFF44,
                ly,
                &feedback,
                "entered VBlank"
            });
        }

        const auto audioFrameCounter = cpu_.cpu().audioFrameCounter();
        if (audioFrameCounter != lastAudioFrameCounter_) {
            lastAudioFrameCounter_ = audioFrameCounter;
            emitMachineEvent(BMMQ::MachineEvent{
                BMMQ::MachineEventType::AudioFrameReady,
                BMMQ::PluginCategory::Audio,
                stepCounter_,
                0xFF26u,
                context_.read8(0xFF26u),
                &feedback,
                "apu frame mixed"
            });
        }

        lastLy_ = ly;
    }

    uint16_t readRegisterPair(std::string_view id) const override {
        const auto* descriptor = cpu_.cpu().getMemory().file.findDescriptor(id);
        if (descriptor == nullptr || !descriptor->isPair) {
            throw std::invalid_argument("register is not a pair");
        }
        auto* entry = cpu_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return entry->reg->value;
    }

    BMMQ::RuntimeContext& runtimeContext() override {
        return context_;
    }

    const BMMQ::RuntimeContext& runtimeContext() const override {
        return context_;
    }

    std::optional<uint32_t> currentDigitalInputMask() const override {
        if (!lastDigitalInputMask_.has_value()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(*lastDigitalInputMask_);
    }

    std::vector<int16_t> recentAudioSamples() const override {
        return cpu_.cpu().copyRecentAudioSamples();
    }

    uint32_t audioSampleRate() const override {
        return cpu_.cpu().audioSampleRate();
    }

    uint64_t audioFrameCounter() const override {
        return cpu_.cpu().audioFrameCounter();
    }

    uint32_t clockHz() const override {
        return cpu_.cpu().clockHz();
    }

    void attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) override {
        BMMQ::Plugin::validateExecutorPolicyStartup(policy);
        activePolicy_ = &policy;
        context_.refreshExecutionMode();
    }

    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

private:
    void pollInputPlugins() {
        if (inputService().state() == BMMQ::InputLifecycleState::Active) {
            (void)inputService().pollActiveAdapter(inputGeneration_);
            if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
                const auto pressedMask = static_cast<uint8_t>(*committedInput & 0x00FFu);
                if (!lastPolledDigitalInput_.has_value() || *lastPolledDigitalInput_ != pressedMask) {
                    setJoypadState(pressedMask);
                }
                return;
            }
        }

        if (pluginManager_.size() == 0) {
            return;
        }

        if (const auto sampledInput = pluginManager_.sampleDigitalInput(view()); sampledInput.has_value()) {
            const auto pressedMask = static_cast<uint8_t>(*sampledInput & 0x00FFu);
            if (!lastPolledDigitalInput_.has_value() || *lastPolledDigitalInput_ != pressedMask) {
                setJoypadState(pressedMask);
            }
        }
    }

    void emitMachineEvent(BMMQ::MachineEvent event) {
        if (pluginManager_.size() == 0) {
            return;
        }
        pluginManager_.emit(view(), event);
    }

    void emitIoWriteEvent(uint16_t address, std::span<const uint8_t> value,
                          std::string_view detail = "memory-mapped I/O write") {
        if (value.size() != 1) {
            return;
        }
        const auto category = classifyIoAddress(address);
        if (!category.has_value()) {
            return;
        }
        emitMachineEvent(BMMQ::MachineEvent{
            BMMQ::MachineEventType::MemoryWriteObserved,
            *category,
            stepCounter_,
            address,
            value[0],
            nullptr,
            detail,
        });
    }

    [[nodiscard]] static std::optional<BMMQ::PluginCategory> classifyIoAddress(uint16_t address) {
        if ((address >= 0x8000u && address < 0xA000u) ||
            (address >= 0xFE00u && address < 0xFEA0u) ||
            (address >= 0xFF40u && address <= 0xFF4Bu)) {
            return BMMQ::PluginCategory::Video;
        }
        if ((address >= 0xFF10u && address <= 0xFF26u) ||
            (address >= 0xFF30u && address <= 0xFF3Fu)) {
            return BMMQ::PluginCategory::Audio;
        }
        if (address == 0xFF00u) {
            return BMMQ::PluginCategory::DigitalInput;
        }
        if (address == 0xFF01u || address == 0xFF02u) {
            return BMMQ::PluginCategory::Serial;
        }
        return std::nullopt;
    }

    static constexpr std::array<BMMQ::IoRegionDescriptor, 7> kIoRegionDescriptors{{
        {BMMQ::PluginCategory::Video, 0x8000u, 0x2000u, "VRAM", true, true},
        {BMMQ::PluginCategory::Video, 0xFE00u, 0x00A0u, "OAM", true, true},
        {BMMQ::PluginCategory::Video, 0xFF40u, 0x000Cu, "LCD Registers", true, true},
        {BMMQ::PluginCategory::Audio, 0xFF10u, 0x0017u, "APU Registers", true, true},
        {BMMQ::PluginCategory::Audio, 0xFF30u, 0x0010u, "Wave RAM", true, true},
        {BMMQ::PluginCategory::DigitalInput, 0xFF00u, 0x0001u, "Joypad", true, true},
        {BMMQ::PluginCategory::Serial, 0xFF01u, 0x0002u, "Serial Registers", true, true},
    }};

    static constexpr std::array<uint8_t, 0x100> kDefaultBootRom = [] {
        std::array<uint8_t, 0x100> rom{};
        rom.fill(0x00);
        rom[0x00] = 0x31; // ld sp,$fffe
        rom[0x01] = 0xFE;
        rom[0x02] = 0xFF;
        rom[0x03] = 0x3E; // ld a,$01
        rom[0x04] = 0x01;
        rom[0x05] = 0xE0; // ldh [$50],a
        rom[0x06] = 0x50;
        rom[0x07] = 0xC3; // jp $0100
        rom[0x08] = 0x00;
        rom[0x09] = 0x01;
        // Offsets 0x42-0x45 are intentional non-executed sentinel bytes.
        // They stay readable through the boot ROM overlay so tests can verify
        // that the `0x0000-0x00FF` mapping is still active before `FF50` handoff.
        rom[0x42] = 0x3E;
        rom[0x43] = 0x91;
        rom[0x44] = 0xE0;
        rom[0x45] = 0x40;
        return rom;
    }();

    enum class CartridgeController {
        None,
        MBC1,
        MBC2,
        MBC3,
        MBC5,
    };

    [[nodiscard]] static std::size_t ramSizeForHeader(uint8_t ramSizeCode,
                                                      CartridgeController controller) {
        if (controller == CartridgeController::MBC2) {
            return 0x0200u;
        }

        switch (ramSizeCode) {
        case 0x00:
            return 0;
        case 0x01:
            return 0x0800u;
        case 0x02:
            return 0x2000u;
        case 0x03:
            return 0x8000u;
        case 0x04:
            return 0x20000u;
        case 0x05:
            return 0x10000u;
        default:
            return 0;
        }
    }

    void configureCartridge(const std::vector<uint8_t>& bytes) {
        const auto type = bytes.size() > 0x147 ? bytes[0x147] : 0x00;
        const auto ramSizeCode = bytes.size() > 0x149 ? bytes[0x149] : 0x00;
        switch (type) {
        case 0x00:
            controller_ = CartridgeController::None;
            break;
        case 0x01:
        case 0x02:
        case 0x03:
            controller_ = CartridgeController::MBC1;
            break;
        case 0x05:
        case 0x06:
            controller_ = CartridgeController::MBC2;
            break;
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            controller_ = CartridgeController::MBC3;
            break;
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
            controller_ = CartridgeController::MBC5;
            break;
        default:
            controller_ = CartridgeController::None;
            if (bytes.size() > 0x8000) {
                throw std::invalid_argument("ROM exceeds first-milestone Game Boy ROM window");
            }
            break;
        }

        if (controller_ == CartridgeController::None && bytes.size() > 0x8000) {
            throw std::invalid_argument("ROM exceeds first-milestone Game Boy ROM window");
        }

        romBankCount_ = (bytes.size() + 0x3FFFu) / 0x4000u;
        currentRomBank_ = romBankCount_ > 1 ? 1 : 0;
        currentRamBank_ = 0;
        ramEnabled_ = false;
        hasRtc_ = (type == 0x0Fu || type == 0x10u);
        selectedRtcRegister_ = 0xFFu;
        rtcLatched_ = false;
        cartridgeRam_.assign(ramSizeForHeader(ramSizeCode, controller_), 0x00u);
        mbc1LowBankBits_ = 1;
        mbc1UpperBankBits_ = 0;
        mbc1BankingModeSelect_ = false;
        mbc5HighBankBit_ = 0;
    }

    void installVisibleRomBanks() {
        installRomBankWindow(0x0000, fixedBankIndex());
        installRomBankWindow(0x4000, switchableBankIndex());
    }

    void installRomBankWindow(uint16_t base, std::size_t bankIndex) {
        std::vector<uint8_t> window(0x4000, 0xFF);
        const auto romBytes = rom_.bytes();
        const auto offset = bankIndex * 0x4000u;
        if (offset < romBytes.size()) {
            const auto count = std::min<std::size_t>(window.size(), romBytes.size() - offset);
            std::copy_n(romBytes.begin() + static_cast<std::ptrdiff_t>(offset), count, window.begin());
        }
        memoryMap_.installRom(window, base);
    }

    [[nodiscard]] std::size_t fixedBankIndex() const {
        if (controller_ == CartridgeController::MBC1 && mbc1BankingModeSelect_) {
            return ((static_cast<std::size_t>(mbc1UpperBankBits_) << 5) % romBankCount_);
        }
        return 0;
    }

    [[nodiscard]] std::size_t switchableBankIndex() const {
        if (romBankCount_ <= 1) {
            return 0;
        }

        switch (controller_) {
        case CartridgeController::None:
            return romBankCount_ > 1 ? 1 : 0;
        case CartridgeController::MBC1: {
            std::size_t bank = (static_cast<std::size_t>(mbc1UpperBankBits_) << 5) |
                               static_cast<std::size_t>(mbc1LowBankBits_ & 0x1Fu);
            bank &= 0x7Fu;
            bank %= romBankCount_;
            if ((bank & 0x1Fu) == 0) {
                bank = (bank + 1) % romBankCount_;
            }
            return bank == 0 ? 1 % romBankCount_ : bank;
        }
        case CartridgeController::MBC2:
        case CartridgeController::MBC3:
            return currentRomBank_ % romBankCount_ == 0 ? 1 % romBankCount_ : currentRomBank_ % romBankCount_;
        case CartridgeController::MBC5:
            return currentRomBank_ % romBankCount_;
        }

        return 0;
    }

    [[nodiscard]] std::size_t selectedRamBankIndex() const {
        const std::size_t bankSize = controller_ == CartridgeController::MBC2 ? 0x0200u : 0x2000u;
        if (bankSize == 0 || cartridgeRam_.empty()) {
            return 0;
        }
        const auto bankCount = std::max<std::size_t>(std::size_t{1}, cartridgeRam_.size() / bankSize);

        switch (controller_) {
        case CartridgeController::MBC1:
            return mbc1BankingModeSelect_ ? (mbc1UpperBankBits_ % bankCount) : 0;
        case CartridgeController::MBC3:
        case CartridgeController::MBC5:
            return currentRamBank_ % bankCount;
        case CartridgeController::MBC2:
        case CartridgeController::None:
            return 0;
        }

        return 0;
    }

    [[nodiscard]] bool rtcRegisterSelected() const {
        return hasRtc_ && selectedRtcRegister_ >= 0x08u && selectedRtcRegister_ <= 0x0Cu;
    }

    bool handleCartridgeRamRead(uint16_t address, std::span<uint8_t> value) const {
        if (address < 0xA000u || address >= 0xC000u || controller_ == CartridgeController::None) {
            return false;
        }

        if (!ramEnabled_) {
            std::fill(value.begin(), value.end(), static_cast<uint8_t>(0xFF));
            return true;
        }

        if (rtcRegisterSelected()) {
            const auto rtcIndex = static_cast<std::size_t>(selectedRtcRegister_ - 0x08u);
            const auto rtcValue = rtcRegisters_[rtcIndex];
            std::fill(value.begin(), value.end(), rtcValue);
            return true;
        }

        if (cartridgeRam_.empty()) {
            std::fill(value.begin(), value.end(), static_cast<uint8_t>(0xFF));
            return true;
        }

        const std::size_t bankSize = controller_ == CartridgeController::MBC2 ? 0x0200u : 0x2000u;
        const auto bankIndex = selectedRamBankIndex();
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto localOffset = controller_ == CartridgeController::MBC2
                ? ((static_cast<std::size_t>(address - 0xA000u) + i) & 0x01FFu)
                : (static_cast<std::size_t>(address - 0xA000u) + i);
            const auto ramIndex = bankIndex * bankSize + localOffset;
            if (ramIndex >= cartridgeRam_.size()) {
                value[i] = 0xFFu;
            } else if (controller_ == CartridgeController::MBC2) {
                value[i] = static_cast<uint8_t>(0xF0u | (cartridgeRam_[ramIndex] & 0x0Fu));
            } else {
                value[i] = cartridgeRam_[ramIndex];
            }
        }
        return true;
    }

    bool handleCartridgeRamWrite(uint16_t address, std::span<const uint8_t> value) {
        if (address < 0xA000u || address >= 0xC000u || controller_ == CartridgeController::None) {
            return false;
        }

        if (!ramEnabled_) {
            return true;
        }

        if (rtcRegisterSelected()) {
            const auto rtcIndex = static_cast<std::size_t>(selectedRtcRegister_ - 0x08u);
            rtcRegisters_[rtcIndex] = value.front();
            return true;
        }

        if (cartridgeRam_.empty()) {
            return true;
        }

        const std::size_t bankSize = controller_ == CartridgeController::MBC2 ? 0x0200u : 0x2000u;
        const auto bankIndex = selectedRamBankIndex();
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto localOffset = controller_ == CartridgeController::MBC2
                ? ((static_cast<std::size_t>(address - 0xA000u) + i) & 0x01FFu)
                : (static_cast<std::size_t>(address - 0xA000u) + i);
            const auto ramIndex = bankIndex * bankSize + localOffset;
            if (ramIndex >= cartridgeRam_.size()) {
                continue;
            }
            cartridgeRam_[ramIndex] = controller_ == CartridgeController::MBC2
                ? static_cast<uint8_t>(value[i] & 0x0Fu)
                : value[i];
        }
        return true;
    }

    bool handleCartridgeWrite(uint16_t address, std::span<const uint8_t> value) {
        if (value.size() != 1 || address >= 0x8000 || controller_ == CartridgeController::None) {
            return false;
        }

        const auto data = value[0];
        switch (controller_) {
        case CartridgeController::MBC1:
            if (address < 0x2000) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return true;
            }
            if (address < 0x4000) {
                mbc1LowBankBits_ = data & 0x1Fu;
                if (mbc1LowBankBits_ == 0) {
                    mbc1LowBankBits_ = 1;
                }
                installVisibleRomBanks();
                return true;
            }
            if (address < 0x6000) {
                mbc1UpperBankBits_ = data & 0x03u;
                installVisibleRomBanks();
                return true;
            }
            mbc1BankingModeSelect_ = (data & 0x01u) != 0;
            installVisibleRomBanks();
            return true;
        case CartridgeController::MBC2:
            if (address < 0x2000) {
                if ((address & 0x0100u) == 0u) {
                    ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                }
                return true;
            }
            if (address < 0x4000) {
                if ((address & 0x0100u) != 0) {
                    currentRomBank_ = data & 0x0Fu;
                    if (currentRomBank_ == 0) {
                        currentRomBank_ = 1;
                    }
                    installVisibleRomBanks();
                }
                return true;
            }
            return true;
        case CartridgeController::MBC3:
            if (address < 0x2000) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return true;
            }
            if (address < 0x4000) {
                currentRomBank_ = data & 0x7Fu;
                if (currentRomBank_ == 0) {
                    currentRomBank_ = 1;
                }
                installVisibleRomBanks();
                return true;
            }
            if (address < 0x6000) {
                if (data <= 0x03u) {
                    currentRamBank_ = data & 0x03u;
                    selectedRtcRegister_ = 0xFFu;
                } else if (hasRtc_ && data >= 0x08u && data <= 0x0Cu) {
                    selectedRtcRegister_ = data;
                }
                return true;
            }
            rtcLatched_ = (data & 0x01u) != 0u;
            return true;
        case CartridgeController::MBC5:
            if (address < 0x2000) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return true;
            }
            if (address < 0x3000) {
                currentRomBank_ = (currentRomBank_ & 0x100u) | data;
                installVisibleRomBanks();
                return true;
            }
            if (address < 0x4000) {
                mbc5HighBankBit_ = data & 0x01u;
                currentRomBank_ = (static_cast<std::size_t>(mbc5HighBankBit_) << 8) | (currentRomBank_ & 0xFFu);
                installVisibleRomBanks();
                return true;
            }
            if (address < 0x6000) {
                currentRamBank_ = data & 0x0Fu;
                return true;
            }
            return true;
        case CartridgeController::None:
            return false;
        }

        return false;
    }

    bool handleSpecialRead(uint16_t address, std::span<uint8_t> value) const {
        if (cpu_.cpu().handleMemoryRead(address, value)) {
            return true;
        }
        if (bootRomMapped_ && address < bootRom_.size()) {
            const auto count = std::min<std::size_t>(value.size(), bootRom_.size() - address);
            std::copy_n(bootRom_.begin() + static_cast<std::ptrdiff_t>(address), count, value.begin());
            return true;
        }
        if (handleCartridgeRamRead(address, value)) {
            return true;
        }
        return false;
    }

    bool handleSpecialWrite(uint16_t address, std::span<const uint8_t> value) {
        if (cpu_.cpu().handleMemoryWrite(address, value)) {
            emitIoWriteEvent(address, value);
            return true;
        }
        if (value.size() == 1 && address == 0xFF50) {
            if (value[0] != 0) {
                if (bootRomMapped_ && context_.readRegister16(GB::RegisterId::PC) < 0x0100) {
                    bootEntryPending_ = true;
                }
                bootRomMapped_ = false;
            }
            const uint8_t bootControl = bootRomMapped_ ? 0x00u : 0x01u;
            memoryMap_.storage().load(std::span<const uint8_t>(&bootControl, 1), 0xFF50);
            emitMachineEvent(BMMQ::MachineEvent{
                BMMQ::MachineEventType::BootRomVisibilityChanged,
                BMMQ::PluginCategory::System,
                stepCounter_,
                address,
                bootControl,
                nullptr,
                bootRomMapped_ ? "boot ROM mapped" : "boot ROM unmapped"
            });
            return true;
        }
        if (handleCartridgeRamWrite(address, value)) {
            return true;
        }
        if (handleCartridgeWrite(address, value)) {
            emitIoWriteEvent(address, value, "cartridge or mapped I/O write");
            return true;
        }
        if (classifyIoAddress(address).has_value()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                const auto currentAddress = static_cast<uint16_t>(address + i);
                memoryMap_.storage().load(std::span<const uint8_t>(&value[i], 1), currentAddress);
                syncHardwareRegisterMirror(currentAddress, std::span<const uint8_t>(&value[i], 1));
            }
            emitIoWriteEvent(address, value);
            return true;
        }
        syncHardwareRegisterMirror(address, value);
        return false;
    }

    void syncHardwareRegisterMirror(uint16_t address, std::span<const uint8_t> value) {
        if (value.empty()) {
            return;
        }

        auto& registerFile = cpu_.cpu().getMemory().file;
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto currentAddress = static_cast<uint16_t>(address + i);
            for (const auto& spec : GB::HardwareRegisters::kSpecs) {
                if (spec.address != currentAddress) {
                    continue;
                }
                if (auto* entry = registerFile.findRegister(spec.name); entry != nullptr && entry->reg != nullptr) {
                    entry->reg->value = value[i];
                }
                break;
            }
        }
    }

    void initializeDmgStartupRegisters() {
        auto& core = cpu_.cpu();
        core.setIme(false);
        core.setStopFlag(false);
        core.clearHaltFlag();
        core.resetDivider();
        core.setJoypadState(0x00u);

        const auto seedIoRegister = [this](std::string_view name, uint8_t value) {
            auto& registerFile = cpu_.cpu().getMemory().file;
            if (auto* entry = registerFile.findRegister(name); entry != nullptr && entry->reg != nullptr) {
                entry->reg->value = value;
            }
            if (const auto* descriptor = registerFile.findDescriptor(name);
                descriptor != nullptr && descriptor->mappedAddress.has_value()) {
                memoryMap_.storage().load(std::span<const uint8_t>(&value, 1), *descriptor->mappedAddress);
            }
        };

        context_.writeRegister16(GB::RegisterId::AF, 0x01B0);
        context_.writeRegister16(GB::RegisterId::BC, 0x0013);
        context_.writeRegister16(GB::RegisterId::DE, 0x00D8);
        context_.writeRegister16(GB::RegisterId::HL, 0x014D);
        context_.writeRegister16(GB::RegisterId::SP, 0xFFFE);
        context_.writeRegister16(GB::RegisterId::PC, 0x0100);

        context_.write8(0xFF00, 0xCF);

        static constexpr std::array<std::pair<std::string_view, uint8_t>, 37> kPostBootIoDefaults{{
            {"SB", 0x00},
            {"SC", 0x7E},
            {"TIMA", 0x00},
            {"TMA", 0x00},
            {"TAC", 0xF8},
            {"IF", 0xE1},
            {"NR10", 0x80},
            {"NR11", 0xBF},
            {"NR12", 0xF3},
            {"NR14", 0xBF},
            {"NR21", 0x3F},
            {"NR22", 0x00},
            {"NR24", 0xBF},
            {"NR30", 0x7F},
            {"NR31", 0xFF},
            {"NR32", 0x9F},
            {"NR34", 0xBF},
            {"NR41", 0xFF},
            {"NR42", 0x00},
            {"NR43", 0x00},
            {"NR44", 0xBF},
            {"NR50", 0x77},
            {"NR51", 0xF3},
            {"NR52", 0xF1},
            {"LCDC", 0x91},
            {"STAT", 0x85},
            {"SCY", 0x00},
            {"SCX", 0x00},
            {"LY", 0x00},
            {"LYC", 0x00},
            {"DMA", 0xFF},
            {"BGP", 0xFC},
            {"OBP0", 0xFF},
            {"OBP1", 0xFF},
            {"WY", 0x00},
            {"WX", 0x00},
            {"IE", 0x00},
        }};
        for (const auto& [name, value] : kPostBootIoDefaults) {
            seedIoRegister(name, value);
        }
        cpu_.cpu().resetApu();
        lastAudioFrameCounter_ = cpu_.cpu().audioFrameCounter();
    }

    void configureMemoryMap() {
        memoryMap_.reset();
        memoryMap_.mapRom(0x0000, 0x4000);
        memoryMap_.mapRom(0x4000, 0x4000);
        memoryMap_.mapRange(0x8000, 0x2000, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xa000, 0x2000, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xc000, 0x2000, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xe000, 0x1e00, BMMQ::memAccess::Unmapped);
        memoryMap_.mapRange(0xfe00, 0x00a0, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xfea0, 0x0060, BMMQ::memAccess::Unmapped);
        memoryMap_.mapRange(0xff00, 0x004c, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xff4c, 0x0001, BMMQ::memAccess::Unmapped);
        memoryMap_.mapRange(0xff4d, 0x001f, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xff6c, 0x0014, BMMQ::memAccess::Unmapped);
        memoryMap_.mapRange(0xff80, 0x007f, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xffff, 0x0001, BMMQ::memAccess::ReadWrite);
        memoryMap_.storage().setAddressTranslator([](uint16_t address) {
            if (address >= 0xE000 && address <= 0xFDFF) {
                return static_cast<uint16_t>(address - 0x2000);
            }
            return address;
        });
        memoryMap_.storage().setReadInterceptor([this](uint16_t address, std::span<uint8_t> value) {
            return handleSpecialRead(address, value);
        });
        memoryMap_.storage().setWriteInterceptor([this](uint16_t address, std::span<const uint8_t> value) {
            return handleSpecialWrite(address, value);
        });
        const uint8_t bootControl = bootRomMapped_ ? 0x00u : 0x01u;
        memoryMap_.storage().load(std::span<const uint8_t>(&bootControl, 1), 0xFF50);
    }

    BMMQ::RomImage rom_;
    BMMQ::MemoryMap memoryMap_;
    bool romLoaded_ = false;
    std::array<uint8_t, 0x100> bootRom_ = kDefaultBootRom;
    bool bootRomMapped_ = false;
    bool bootEntryPending_ = false;
    CartridgeController controller_ = CartridgeController::None;
    std::size_t romBankCount_ = 0;
    std::size_t currentRomBank_ = 1;
    std::vector<uint8_t> cartridgeRam_;
    std::size_t currentRamBank_ = 0;
    bool ramEnabled_ = false;
    bool hasRtc_ = false;
    uint8_t selectedRtcRegister_ = 0xFFu;
    bool rtcLatched_ = false;
    std::array<uint8_t, 5> rtcRegisters_{};
    uint8_t mbc1LowBankBits_ = 1;
    uint8_t mbc1UpperBankBits_ = 0;
    bool mbc1BankingModeSelect_ = false;
    uint8_t mbc5HighBankBit_ = 0;
    uint64_t stepCounter_ = 0;
    uint64_t inputGeneration_ = 1;
    uint8_t lastLy_ = 0;
    uint64_t lastAudioFrameCounter_ = 0;
    std::optional<uint8_t> lastDigitalInputMask_;
    std::optional<uint8_t> lastPolledDigitalInput_;
    LR3592_PluginRuntime cpu_;
    BMMQ::PluginManager pluginManager_;
    BMMQ::Plugin::DefaultStepPolicy defaultPolicy_;
    BMMQ::Plugin::IExecutorPolicyPlugin* activePolicy_;
    GameBoyRuntimeContext context_;
};

#endif // GAMEBOY_MACHINE_HPP
