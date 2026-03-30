#ifndef GAMEBOY_MACHINE_HPP
#define GAMEBOY_MACHINE_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "../../machine/Machine.hpp"
#include "../../machine/MemoryMap.hpp"
#include "../../machine/RomImage.hpp"
#include "../../machine/RuntimeContext.hpp"
#include "gameboy_plugin_runtime.hpp"

class GameBoyRuntimeContext final : public BMMQ::RuntimeContext {
public:
    explicit GameBoyRuntimeContext(LR3592_PluginRuntime& runtime)
        : runtime_(runtime) {}

    FetchBlock fetch() override {
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
};

class GameBoyMachine final : public BMMQ::Machine {
public:
    GameBoyMachine()
        : context_(cpu_) {}

    void loadRom(const std::vector<uint8_t>& bytes) override {
        cpu_.cpu().loadProgram(bytes);
    }

    void stepBaseline() {
        step();
    }

    uint16_t readRegisterPair(std::string_view name) override {
        auto* entry = cpu_.cpu().getMemory().file.findRegister(name);
        if (entry == nullptr || entry->second == nullptr) return 0;

        auto* reg = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(entry->second);
        return reg != nullptr ? reg->value : 0;
    }

    bool hasCpu() const { return true; }
    bool hasMemoryMap() const { return true; }

    BMMQ::RuntimeContext& runtimeContext() override {
        return context_;
    }

private:
    LR3592_PluginRuntime cpu_;
    GameBoyRuntimeContext context_;
};

#endif // GAMEBOY_MACHINE_HPP
