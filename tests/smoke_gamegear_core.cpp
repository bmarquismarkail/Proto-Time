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

std::vector<uint8_t> makeFrontendProofRom()
{
    std::vector<uint8_t> rom(0x4000u, 0x00u);
    std::size_t pc = 0u;
    auto emit8 = [&](uint8_t value) {
        rom[pc++] = value;
    };
    auto emit16 = [&](uint16_t value) {
        emit8(static_cast<uint8_t>(value & 0x00FFu));
        emit8(static_cast<uint8_t>((value >> 8) & 0x00FFu));
    };
    auto emitLoadHlAndByte = [&](uint16_t address, uint8_t value) {
        emit8(0x21u); // LD HL,nn
        emit16(address);
        emit8(0x36u); // LD (HL),n
        emit8(value);
    };

    emitLoadHlAndByte(0xFF40u, 0x91u);
    emitLoadHlAndByte(0xFF47u, 0xE4u);

    emit8(0x21u); // LD HL,0x8000
    emit16(0x8000u);
    for (int row = 0; row < 8; ++row) {
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u); // INC HL
        emit8(0x36u);
        emit8(0x00u);
        emit8(0x23u); // INC HL
    }

    emit8(0x21u); // LD HL,0x9800
    emit16(0x9800u);
    emit8(0x36u);
    emit8(0x00u);

    emit8(0x21u); // LD HL,0x8010
    emit16(0x8010u);
    for (int row = 0; row < 8; ++row) {
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u); // INC HL
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u); // INC HL
    }

    emit8(0x21u); // LD HL,0xFE00
    emit16(0xFE00u);
    emit8(0x36u);
    emit8(32u);
    emit8(0x23u);
    emit8(0x36u);
    emit8(16u);
    emit8(0x23u);
    emit8(0x36u);
    emit8(0x01u);
    emit8(0x23u);
    emit8(0x36u);
    emit8(0x00u);

    emit8(0x21u); // LD HL,0xC000
    emit16(0xC000u);
    const uint16_t inputLoopAddress = static_cast<uint16_t>(pc);
    emit8(0xDBu); // IN A,(n)
    emit8(0xDCu);
    emit8(0x77u); // LD (HL),A
    emit8(0xC3u); // JP nn
    emit16(inputLoopAddress);

    return rom;
}

bool stepUntil(BMMQ::GameGearMachine& machine, int maxSteps, auto&& predicate)
{
    for (int i = 0; i < maxSteps; ++i) {
        machine.step();
        if (predicate()) {
            return true;
        }
    }
    return false;
}

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
    const std::vector<uint8_t> rom = makeFrontendProofRom();
    gg.loadRom(rom);
    assert(stepUntil(gg, 256, [&] {
        return gg.runtimeContext().read8(0xFF40u) == 0x91u;
    }));
    assert(gg.runtimeContext().readRegister16("PC") != 0u);
    gg.runtimeContext().writeRegister16("SP", 0xD123u);
    assert(gg.runtimeContext().readRegister16("SP") == 0xD123u);
    assert(gg.readRegisterPair("SP") == 0xD123u);
    assert(stepUntil(gg, 2048, [&] {
        return gg.runtimeContext().read8(0xFE02u) == 0x01u;
    }));
    assert(gg.runtimeContext().read8(0x8000u) == 0xFFu);
    assert(gg.runtimeContext().read8(0x8001u) == 0x00u);
    assert(gg.runtimeContext().read8(0x8010u) == 0xFFu);
    assert(gg.runtimeContext().read8(0x8011u) == 0xFFu);
    assert(gg.runtimeContext().read8(0x9800u) == 0x00u);
    assert(gg.runtimeContext().read8(0xFE00u) == 32u);
    assert(gg.runtimeContext().read8(0xFE01u) == 16u);
    assert(gg.runtimeContext().read8(0xFE02u) == 0x01u);
    assert(gg.runtimeContext().read8(0xFF47u) == 0xE4u);

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
    assert(stepUntil(gg, 16, [&] {
        return gg.runtimeContext().read8(0xC000u) != 0x00u;
    }));
    assert((gg.runtimeContext().read8(0xC000u) & 0x08u) == 0u);
    assert((gg.runtimeContext().read8(0xC000u) & 0x10u) == 0u);
    assert((gg.runtimeContext().read8(0xC000u) & 0x80u) == 0u);

    puts("Game Gear core basic smoke test passed.");
    return 0;
}
