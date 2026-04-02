#ifndef GAMEBOY_MACHINE_HPP
#define GAMEBOY_MACHINE_HPP

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string_view>
#include <stdexcept>
#include <vector>

#include "../../machine/Machine.hpp"
#include "../../machine/MemoryMap.hpp"
#include "../../machine/RegisterId.hpp"
#include "../../machine/RomImage.hpp"
#include "../../machine/RuntimeContext.hpp"
#include "gameboy_plugin_runtime.hpp"

class GameBoyRuntimeContext final : public BMMQ::RuntimeContext {
public:
    GameBoyRuntimeContext(
        LR3592_PluginRuntime& runtime,
        BMMQ::MemoryMap& memoryMap,
        const bool& romLoaded,
        BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy)
        : runtime_(runtime), memoryMap_(memoryMap), romLoaded_(romLoaded), activePolicy_(activePolicy) {}

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

    DataType read8(AddressType address) const override {
        return memoryMap_.read8(resolveEchoAddress(address));
    }

    void write8(AddressType address, DataType value) override {
        memoryMap_.write8(resolveEchoAddress(address), value);
    }

    uint16_t readRegister16(BMMQ::RegisterId id) const override {
        auto* entry = requireRegisterEntry(id);
        return entry->reg->value;
    }

    void writeRegister16(BMMQ::RegisterId id, uint16_t value) override {
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    uint16_t readRegisterPair(BMMQ::RegisterId id) const override {
        auto* entry = requireRegisterEntry(id);
        ensurePairRegister(entry);
        return entry->reg->value;
    }

    void writeRegisterPair(BMMQ::RegisterId id, uint16_t value) override {
        auto* entry = requireRegisterEntry(id);
        ensurePairRegister(entry);
        entry->reg->value = value;
    }

    const BMMQ::CpuFeedback& getLastFeedback() const override {
        return runtime_.getLastFeedback();
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

private:
    static AddressType resolveEchoAddress(AddressType address) {
        if (address >= 0xE000 && address <= 0xFDFF) {
            return static_cast<AddressType>(address - 0x2000);
        }
        return address;
    }

    using RegisterEntry = decltype(std::declval<BMMQ::RegisterFile<uint16_t>&>().findRegister(BMMQ::RegisterId::AF));

    RegisterEntry requireRegisterEntry(BMMQ::RegisterId id) const {
        if (id != BMMQ::RegisterId::AF &&
            id != BMMQ::RegisterId::BC &&
            id != BMMQ::RegisterId::DE &&
            id != BMMQ::RegisterId::HL &&
            id != BMMQ::RegisterId::SP &&
            id != BMMQ::RegisterId::PC) {
            throw std::invalid_argument("register is not part of visible 16-bit machine state");
        }
        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return entry;
    }

    void ensurePairRegister(RegisterEntry entry) const {
        auto* reg = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(entry->reg.get());
        if (reg == nullptr) {
            throw std::invalid_argument("register is not a pair");
        }
    }

    LR3592_PluginRuntime& runtime_;
    BMMQ::MemoryMap& memoryMap_;
    const bool& romLoaded_;
    BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy_;
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

    void loadRom(const std::vector<uint8_t>& bytes) override {
        if (bytes.empty()) {
            throw std::invalid_argument("ROM must not be empty");
        }

        rom_.load(bytes);
        configureCartridge(bytes);
        configureMemoryMap();
        installVisibleRomBanks();
        initializeDmgStartupRegisters();
        romLoaded_ = true;
    }

    uint16_t readRegisterPair(BMMQ::RegisterId id) const override {
        auto* entry = cpu_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }

        auto* reg = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(entry->reg.get());
        if (reg == nullptr) {
            throw std::invalid_argument("register is not a pair");
        }
        return reg->value;
    }

    BMMQ::RuntimeContext& runtimeContext() override {
        return context_;
    }

    const BMMQ::RuntimeContext& runtimeContext() const override {
        return context_;
    }

    void attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) override {
        BMMQ::Plugin::validateExecutorPolicyStartup(policy);
        activePolicy_ = &policy;
    }

    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

private:
    enum class CartridgeController {
        None,
        MBC1,
        MBC2,
        MBC3,
        MBC5,
    };

    void configureCartridge(const std::vector<uint8_t>& bytes) {
        const auto type = bytes.size() > 0x147 ? bytes[0x147] : 0x00;
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

    bool handleCartridgeWrite(uint16_t address, std::span<const uint8_t> value) {
        if (value.size() != 1 || address >= 0x8000 || controller_ == CartridgeController::None) {
            return false;
        }

        const auto data = value[0];
        switch (controller_) {
        case CartridgeController::MBC1:
            if (address < 0x2000) {
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
            return true;
        case CartridgeController::MBC5:
            if (address < 0x2000) {
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
            return true;
        case CartridgeController::None:
            return false;
        }

        return false;
    }

    bool handleSpecialRead(uint16_t address, std::span<uint8_t> value) const {
        if (address >= 0xFEA0 && address <= 0xFEFF) {
            std::fill(value.begin(), value.end(), static_cast<uint8_t>(0xFF));
            return true;
        }
        return false;
    }

    bool handleSpecialWrite(uint16_t address, std::span<const uint8_t> value) {
        if (address >= 0xFEA0 && static_cast<std::size_t>(address - 0xFEA0u) + value.size() <= 0x60u) {
            return true;
        }
        return handleCartridgeWrite(address, value);
    }

    void initializeDmgStartupRegisters() {
        context_.writeRegister16(BMMQ::RegisterId::AF, 0x01B0);
        context_.writeRegister16(BMMQ::RegisterId::BC, 0x0013);
        context_.writeRegister16(BMMQ::RegisterId::DE, 0x00D8);
        context_.writeRegister16(BMMQ::RegisterId::HL, 0x014D);
        context_.writeRegister16(BMMQ::RegisterId::SP, 0xFFFE);
        context_.writeRegister16(BMMQ::RegisterId::PC, 0x0100);
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
    }

    BMMQ::RomImage rom_;
    BMMQ::MemoryMap memoryMap_;
    bool romLoaded_ = false;
    CartridgeController controller_ = CartridgeController::None;
    std::size_t romBankCount_ = 0;
    std::size_t currentRomBank_ = 1;
    uint8_t mbc1LowBankBits_ = 1;
    uint8_t mbc1UpperBankBits_ = 0;
    bool mbc1BankingModeSelect_ = false;
    uint8_t mbc5HighBankBit_ = 0;
    LR3592_PluginRuntime cpu_;
    BMMQ::Plugin::DefaultStepPolicy defaultPolicy_;
    BMMQ::Plugin::IExecutorPolicyPlugin* activePolicy_;
    GameBoyRuntimeContext context_;
};

#endif // GAMEBOY_MACHINE_HPP
