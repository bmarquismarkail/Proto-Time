#include "cores/gamegear/Z80Interpreter.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr uint8_t kFlagS = 0x80u;
constexpr uint8_t kFlagZ = 0x40u;
constexpr uint8_t kFlagH = 0x10u;
constexpr uint8_t kFlagPV = 0x04u;
constexpr uint8_t kFlagN = 0x02u;
constexpr uint8_t kFlagC = 0x01u;

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
            std::cerr << "smoke_gamegear_z80_halt: " << message << '\n'; \
            return 1; \
        } \
    } while (false)

}

int main() {
    // Scenario A: HALT with IME enabled should be serviced by maskable IRQs.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0x76u; // HALT
        memory[0x0001u] = 0x00u; // NOP

        bool interruptArmed = false;
        bool interruptServed = false;
        cpu.setInterruptRequestProvider([&interruptArmed, &interruptServed]() -> std::optional<uint8_t> {
            if (!interruptArmed || interruptServed) return std::nullopt;
            interruptServed = true;
            return static_cast<uint8_t>(0xAAu);
        });

        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x0000u;

        // Execute HALT
        CHECK_OR_FAIL(cpu.step() == 4u, "HALT should consume 4 cycles on execute");
        CHECK_OR_FAIL(cpu.PC == 0x0001u, "PC should advance past the HALT opcode during fetch");

        // Arm the interrupt now that HALT has executed; next step should service it.
        interruptArmed = true;
        // Next step should detect the pending interrupt and service it.
        CHECK_OR_FAIL(cpu.step() == 11u, "Serviced interrupt should take 11 cycles in IM1");
        CHECK_OR_FAIL(cpu.PC == 0x0038u, "Interrupt should vector to 0x0038 in IM1");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "SP should be decremented by 2 after interrupt push");
        CHECK_OR_FAIL(memory[0xCFFFu] == 0x00u && memory[0xCFFEu] == 0x01u,
                      "Interrupt should have pushed the return address (post-HALT PC)");
        CHECK_OR_FAIL(!cpu.IME && !cpu.IFF1, "IME and IFF1 should be cleared after servicing maskable IRQ");
    }

    // Scenario B: HALT with IME disabled should remain halted even if an IRQ is pending.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0x76u; // HALT
        memory[0x0001u] = 0x3Eu; memory[0x0002u] = 0x14u; // LD A,0x14 (should not execute)

        bool interruptPending = true;
        cpu.setInterruptRequestProvider([&interruptPending]() -> std::optional<uint8_t> {
            if (!interruptPending) return std::nullopt;
            // Interrupt remains pending but IME is disabled so it should not be serviced
            return static_cast<uint8_t>(0xAAu);
        });

        cpu.IME = false; // interrupts disabled
        cpu.IFF1 = true; // still indicate request acceptance if IME were enabled
        cpu.SP = 0xD100u;
        cpu.PC = 0x0000u;

        // Execute HALT
        CHECK_OR_FAIL(cpu.step() == 4u, "HALT should consume 4 cycles on execute (IME off)");
        CHECK_OR_FAIL(cpu.PC == 0x0001u, "PC should advance past the HALT opcode during fetch");

        // Next several steps should remain in HALT (no servicing when IME==false)
        CHECK_OR_FAIL(cpu.step() == 4u, "HALT should consume cycles while waiting (IME off)");
        CHECK_OR_FAIL(cpu.step() == 4u, "HALT should still be active while IME is disabled and IRQ pending");
        // Ensure PC/SP unchanged and no push occurred
        CHECK_OR_FAIL(cpu.PC == 0x0001u, "PC should remain at post-HALT value while halted");
        CHECK_OR_FAIL(cpu.SP == 0xD100u, "SP should not be altered when HALT remains and no interrupt serviced");
        CHECK_OR_FAIL(memory[0xD100u] == 0x00u && memory[0xD101u] == 0x00u, "No stack writes should occur when HALT remains");
    }

    return 0;
}
