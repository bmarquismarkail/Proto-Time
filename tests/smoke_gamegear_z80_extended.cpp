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
            std::cerr << "smoke_gamegear_z80_extended: " << message << '\n'; \
            return 1; \
        } \
    } while (false)

}

int main() {
    // IM2 interrupt handling test. Verify the interpreter constructs the
    // vector using `I` and the provided interrupt vector byte and then
    // jumps to the address stored at that vector table entry.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        // Prepare IM2 vector table entry at 0x1234 -> handler 0x2000
        memory[0x1234u] = 0x00u; // low
        memory[0x1235u] = 0x20u; // high

        bool interruptPending = true;
        cpu.setInterruptRequestProvider([&interruptPending]() -> std::optional<uint8_t> {
            if (!interruptPending) return std::nullopt;
            interruptPending = false;
            return static_cast<uint8_t>(0x34u);
        });

        cpu.I = 0x12u;
        cpu.setInterruptMode(2u);
        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x3000u;

        (void)cpu.step(); // service interrupt

        CHECK_OR_FAIL(cpu.PC == 0x2000u, "IM2 handler PC mismatch");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "IM2 SP mismatch");
        CHECK_OR_FAIL(memory[0xCFFFu] == 0x30u && memory[0xCFFEu] == 0x00u,
                      "IM2 pushed return address bytes mismatch");
    }

    // EI deferred IME enable semantics. EI should enable IME only after the
    // next instruction completes; the pending interrupt should be serviced
    // after that.
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0xFBu; // EI
        memory[0x0001u] = 0x00u; // NOP

        bool interruptPending = true;
        cpu.setInterruptRequestProvider([&interruptPending]() -> std::optional<uint8_t> {
            if (!interruptPending) return std::nullopt;
            interruptPending = false;
            return static_cast<uint8_t>(0xAAu);
        });

        cpu.setInterruptMode(1u);
        cpu.IME = false;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x0000u;

        // Execute EI
        (void)cpu.step();
        CHECK_OR_FAIL(cpu.PC == 0x0001u, "EI did not advance PC");
        CHECK_OR_FAIL(cpu.IME == false, "IME should not be enabled immediately after EI");

        // Execute the instruction following EI; IME becomes true after this
        (void)cpu.step();
        CHECK_OR_FAIL(cpu.IME == true, "IME should be enabled after the instruction following EI");

        // Now the pending interrupt should be serviced (IM1 -> vector 0x0038)
        (void)cpu.step();
        CHECK_OR_FAIL(cpu.PC == 0x0038u, "Interrupt not serviced after EI semantics");
        CHECK_OR_FAIL(cpu.SP == 0xCFFEu, "Interrupt did not push return address");
    }

    return 0;
}
