#include "cores/gamegear/Z80Interpreter.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

Z80Interpreter makeCpu(std::vector<uint8_t>& memory) {
    Z80Interpreter cpu;
    cpu.setMemoryInterface(
        [&](uint16_t address) { return memory[address]; },
        [&](uint16_t address, uint8_t value) { memory[address] = value; }
    );
    cpu.reset();
    return cpu;
}

#define CHECK_OR_FAIL(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "smoke_gamegear_z80_interrupts: " << message << '\n'; \
            return 1; \
        } \
    } while (false)

}

int main() {
    // IM1: Maskable interrupt in IM1 should vector to 0x0038, push PC, and clear IME/IFFs.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        bool irqDelivered = true;
        cpu.setInterruptRequestProvider([&irqDelivered]() -> std::optional<uint8_t> {
            if (!irqDelivered) return std::nullopt;
            irqDelivered = false;
            return static_cast<uint8_t>(0xAAu);
        });

        cpu.setInterruptMode(1u);
        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.IFF2 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x1234u;

        const uint32_t cycles = cpu.step();
        CHECK_OR_FAIL(cycles == 11u, "IM1 interrupt should consume 11 cycles");
        CHECK_OR_FAIL(cpu.PC == 0x0038u, "IM1 should vector to 0x0038");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "IM1 should push PC onto the stack");
        CHECK_OR_FAIL(memory[0xCFFFu] == 0x12u && memory[0xCFFEu] == 0x34u,
                      "IM1 push should store return PC high/low at SP+1/SP");
        CHECK_OR_FAIL(!cpu.IME && !cpu.IFF1 && !cpu.IFF2, "Interpreter clears IME/IFFs after servicing maskable IRQ");
    }

    // DI: immediate disable — a pending interrupt armed after DI should not be serviced.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        // DI at 0x0000, NOP at 0x0001
        memory[0x0000u] = 0xF3u; memory[0x0001u] = 0x00u;
        auto cpu = makeCpu(memory);

        bool irqArmed = false;
        cpu.setInterruptRequestProvider([&irqArmed]() -> std::optional<uint8_t> {
            if (!irqArmed) return std::nullopt;
            return static_cast<uint8_t>(0xAAu);
        });

        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD100u;
        cpu.PC = 0x0000u;

        // Execute DI; it should clear IME/IFFs immediately.
        CHECK_OR_FAIL(cpu.step() == 4u, "DI should execute in 4 cycles");
        CHECK_OR_FAIL(!cpu.IME && !cpu.IFF1, "DI should clear IME and IFF1 immediately");

        // Arm the IRQ after DI; since IME==false, the CPU should remain unserviced.
        irqArmed = true;
        CHECK_OR_FAIL(cpu.step() == 4u, "With IME cleared, pending IRQ should not be serviced (NOP executes)");
        CHECK_OR_FAIL(cpu.PC == 0x0002u, "PC should advance past NOP when IRQ is not serviced");
        CHECK_OR_FAIL(cpu.SP == 0xD100u, "SP should not be changed when IRQ is not serviced");
    }

    // IM0 behavior (simplified heuristic implemented): when the supplied opcode
    // matches a RST pattern (0xC7/CF/D7/...), interpreter jumps to the RST vector;
    // otherwise it falls back to 0x0038. These tests document current behavior.
    {
        // IM0, RST vector case (e.g., 0xCF -> RST 0x08)
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        bool irqDelivered = true;
        cpu.setInterruptRequestProvider([&irqDelivered]() -> std::optional<uint8_t> {
            if (!irqDelivered) return std::nullopt;
            irqDelivered = false;
            return static_cast<uint8_t>(0xCFu); // RST 0x08 opcode on the data bus
        });

        cpu.setInterruptMode(0u);
        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x2000u;

        CHECK_OR_FAIL(cpu.step() == 11u, "IM0 RST-case should take 11 cycles");
        CHECK_OR_FAIL(cpu.PC == 0x0008u, "IM0 RST-case should load RST vector address (0x0008)");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "IM0 should push return PC on the stack");
        CHECK_OR_FAIL(!cpu.IME && !cpu.IFF1 && !cpu.IFF2, "IM0 handler clears IME/IFFs in this implementation");
    }

    {
        // IM0, non-RST fallback should vector to 0x0038 (as implemented)
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        bool irqDelivered = true;
        cpu.setInterruptRequestProvider([&irqDelivered]() -> std::optional<uint8_t> {
            if (!irqDelivered) return std::nullopt;
            irqDelivered = false;
            return static_cast<uint8_t>(0xAAu); // not a RST opcode
        });

        cpu.setInterruptMode(0u);
        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x4000u;

        CHECK_OR_FAIL(cpu.step() == 11u, "IM0 non-RST fallback should take 11 cycles");
        CHECK_OR_FAIL(cpu.PC == 0x0038u, "IM0 non-RST fallback should vector to 0x0038 in this implementation");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "IM0 fallback should push return PC on the stack");
    }

    return 0;
}
