#ifndef GAMEBOY_MACHINE_HPP
#define GAMEBOY_MACHINE_HPP

#include <cstdint>
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
        return memoryMap_.read8(address);
    }

    void write8(AddressType address, DataType value) override {
        memoryMap_.write8(address, value);
    }

    uint16_t readRegisterPair(BMMQ::RegisterId id) const override {
        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        auto* reg = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(entry->reg.get());
        if (reg == nullptr) {
            throw std::invalid_argument("register is not a pair");
        }
        return reg->value;
    }

    void writeRegisterPair(BMMQ::RegisterId id, uint16_t value) override {
        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        auto* reg = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(entry->reg.get());
        if (reg == nullptr) {
            throw std::invalid_argument("register is not a pair");
        }
        reg->value = value;
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

    BMMQ::RuntimeCapabilityProfile capabilityProfile() const override {
        return activePolicy_->capabilityProfile();
    }

private:
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
        if (bytes.size() > 0x8000) {
            throw std::invalid_argument("ROM exceeds first-milestone Game Boy ROM window");
        }

        rom_.load(bytes);
        configureMemoryMap();
        memoryMap_.installRom(bytes, 0x0000);
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
        memoryMap_.mapRange(0xff4c, 0x0034, BMMQ::memAccess::Unmapped);
        memoryMap_.mapRange(0xff80, 0x007f, BMMQ::memAccess::ReadWrite);
        memoryMap_.mapRange(0xffff, 0x0001, BMMQ::memAccess::ReadWrite);
    }

    BMMQ::RomImage rom_;
    BMMQ::MemoryMap memoryMap_;
    bool romLoaded_ = false;
    LR3592_PluginRuntime cpu_;
    BMMQ::Plugin::DefaultStepPolicy defaultPolicy_;
    BMMQ::Plugin::IExecutorPolicyPlugin* activePolicy_;
    GameBoyRuntimeContext context_;
};

#endif // GAMEBOY_MACHINE_HPP
