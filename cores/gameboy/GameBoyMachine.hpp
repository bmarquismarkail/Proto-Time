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
    GameBoyRuntimeContext(LR3592_PluginRuntime& runtime, const bool& romLoaded)
        : runtime_(runtime), romLoaded_(romLoaded) {}

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

    const BMMQ::CpuFeedback& getLastFeedback() const override {
        return runtime_.getLastFeedback();
    }

    BMMQ::ExecutionGuarantee guarantee() const override {
        return BMMQ::ExecutionGuarantee::BaselineFaithful;
    }

private:
    LR3592_PluginRuntime& runtime_;
    const bool& romLoaded_;
};

class GameBoyMachine final : public BMMQ::Machine {
public:
    GameBoyMachine()
        : context_(cpu_, romLoaded_) {
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
    GameBoyRuntimeContext context_;
};

#endif // GAMEBOY_MACHINE_HPP
