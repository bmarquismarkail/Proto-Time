#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/Z80Interpreter.hpp"
#include "machine/InputTypes.hpp"
#include "machine/plugins/input/InputPlugin.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

class FixedInputPlugin final : public BMMQ::IDigitalInputSourcePlugin {
public:
    explicit FixedInputPlugin(BMMQ::InputButtonMask value)
        : value_(value) {}

    BMMQ::InputPluginCapabilities capabilities() const noexcept override {
        return BMMQ::InputPluginCapabilities{
            true,
            false,
            true,
            true,
            false,
            true,
            false,
            false,
            false,
            true
        };
    }

    std::string_view name() const noexcept override {
        return "fixed-gamegear-input";
    }

    bool open() override {
        return true;
    }

    void close() noexcept override {
    }

    std::string_view lastError() const noexcept override {
        return {};
    }

    std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override {
        return value_;
    }

private:
    BMMQ::InputButtonMask value_ = 0u;
};

}

int main() {
    BMMQ::GameGearMachine gg;
    const std::vector<uint8_t> rom = {0x00u, 0x00u, 0x00u, 0x00u};
    gg.loadRom(rom);
    // Step a few cycles to ensure no crash
    for (int i = 0; i < 10; ++i) gg.step();
    assert(gg.runtimeContext().readRegister16("PC") == 10u);
    gg.runtimeContext().writeRegister16("SP", 0xD123u);
    assert(gg.runtimeContext().readRegister16("SP") == 0xD123u);
    gg.runtimeContext().write8(0xC000u, 0x5Au);
    assert(gg.runtimeContext().read8(0xC000u) == 0x5Au);
    assert(gg.readRegisterPair("SP") == 0xD123u);

    GameGearMemoryMap memory;
    const std::vector<uint8_t> mappedRom(0x8000, 0xAAu);
    memory.mapRom(mappedRom.data(), mappedRom.size());
    assert(memory.read(0x00DC) == 0xFFu);
    assert(memory.read(0x7F00) == 0x00u);

    Z80Interpreter cpu;
    assert(cpu.AF == 0u);
    assert(cpu.BC == 0u);
    assert(cpu.DE == 0u);
    assert(cpu.HL == 0u);
    assert(cpu.IX == 0u);
    assert(cpu.IY == 0u);
    assert(cpu.SP == 0u);
    assert(cpu.PC == 0u);
    assert(cpu.AF_ == 0u);
    assert(cpu.BC_ == 0u);
    assert(cpu.DE_ == 0u);
    assert(cpu.HL_ == 0u);
    assert(cpu.I == 0u);
    assert(cpu.R == 0u);
    assert(!cpu.IFF1);
    assert(!cpu.IFF2);
    assert(!cpu.IME);

    bool threwMissingMemoryInterface = false;
    try {
        (void)cpu.step();
    } catch (const std::runtime_error& ex) {
        threwMissingMemoryInterface = std::string_view(ex.what()) == "Z80 memory interface not set";
    }
    assert(threwMissingMemoryInterface);

    std::vector<uint8_t> z80Memory(0x10000u, 0x00u);
    cpu.setMemoryInterface(
        [&](uint16_t address) { return z80Memory[address]; },
        [&](uint16_t address, uint8_t value) { z80Memory[address] = value; }
    );
    cpu.reset();
    assert(cpu.step() == 4u);
    assert(cpu.PC == 0x0001u);

    const auto logicalMask = static_cast<BMMQ::InputButtonMask>(
        BMMQ::inputButtonMask(BMMQ::InputButton::Right) |
        BMMQ::inputButtonMask(BMMQ::InputButton::Button1) |
        BMMQ::inputButtonMask(BMMQ::InputButton::Meta1));
    FixedInputPlugin inputPlugin(logicalMask);
    assert(gg.inputService().attachExternalAdapter(inputPlugin));
    assert(gg.inputService().resume());
    gg.serviceInput();
    assert(gg.currentDigitalInputMask().has_value());
    assert(*gg.currentDigitalInputMask() == logicalMask);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x08u) == 0u);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x10u) == 0u);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x80u) == 0u);

    puts("Game Gear core basic smoke test passed.");
    return 0;
}
