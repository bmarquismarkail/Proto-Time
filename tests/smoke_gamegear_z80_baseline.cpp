#include "cores/gamegear/Z80Interpreter.hpp"

#include <cassert>
#include <cstdint>
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

}

int main() {
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0x01u; memory[0x0001u] = 0x34u; memory[0x0002u] = 0x12u; // LD BC,0x1234
        memory[0x0003u] = 0x11u; memory[0x0004u] = 0x78u; memory[0x0005u] = 0x56u; // LD DE,0x5678
        memory[0x0006u] = 0x21u; memory[0x0007u] = 0xBCu; memory[0x0008u] = 0x9Au; // LD HL,0x9ABC
        memory[0x0009u] = 0x31u; memory[0x000Au] = 0xF0u; memory[0x000Bu] = 0xDFu; // LD SP,0xDFF0
        assert((cpu.step() == 10u) && "LD BC,0x1234 step failed: expected cpu.step() == 10u");
        assert((cpu.BC == 0x1234u) && "LD BC,0x1234 register check failed: expected BC == 0x1234");
        assert((cpu.step() == 10u) && "LD DE,0x5678 step failed: expected cpu.step() == 10u");
        assert((cpu.DE == 0x5678u) && "LD DE,0x5678 register check failed: expected DE == 0x5678");
        assert((cpu.step() == 10u) && "LD HL,0x9ABC step failed: expected cpu.step() == 10u");
        assert((cpu.HL == 0x9ABCu) && "LD HL,0x9ABC register check failed: expected HL == 0x9ABC");
        assert((cpu.step() == 10u) && "LD SP,0xDFF0 step failed: expected cpu.step() == 10u");
        assert((cpu.SP == 0xDFF0u) && "LD SP,0xDFF0 register check failed: expected SP == 0xDFF0");
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0x16u; memory[0x0001u] = 0x12u; // LD D,0x12
        memory[0x0002u] = 0x1Eu; memory[0x0003u] = 0x34u; // LD E,0x34
        memory[0x0004u] = 0x26u; memory[0x0005u] = 0x56u; // LD H,0x56
        memory[0x0006u] = 0x2Eu; memory[0x0007u] = 0x78u; // LD L,0x78
        memory[0x0008u] = 0x7Bu;                           // LD A,E
        memory[0x0009u] = 0x77u;                           // LD (HL),A
        memory[0x000Au] = 0x5Eu;                           // LD E,(HL)

        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(cpu.DE == 0x1234u);
        assert(cpu.HL == 0x5678u);
        assert(cpu.step() == 4u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x34u);
        assert(cpu.step() == 7u);
        assert(memory[0x5678u] == 0x34u);
        assert(cpu.step() == 7u);
        assert(cpu.DE == 0x1234u);
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x4000u] = 0x05u;
        memory[0x0000u] = 0x21u; memory[0x0001u] = 0x00u; memory[0x0002u] = 0x40u; // LD HL,0x4000
        memory[0x0003u] = 0x3Eu; memory[0x0004u] = 0x10u;                           // LD A,0x10
        memory[0x0005u] = 0x86u;                                                   // ADD A,(HL)
        memory[0x0006u] = 0xFEu; memory[0x0007u] = 0x15u;                           // CP 0x15
        memory[0x0008u] = 0x3Eu; memory[0x0009u] = 0xF0u;                           // LD A,0xF0
        memory[0x000Au] = 0xE6u; memory[0x000Bu] = 0x0Fu;                           // AND 0x0F
        memory[0x000Cu] = 0xF6u; memory[0x000Du] = 0x80u;                           // OR 0x80

        assert(cpu.step() == 10u);
        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x15u);
        assert((cpu.AF & 0x00FFu) == 0x00u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x15u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagN | kFlagZ));
        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x00u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagZ | kFlagH | kFlagPV));
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x80u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagS));
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0x3Eu; memory[0x0001u] = 0x7Fu; // LD A,0x7F
        memory[0x0002u] = 0xC6u; memory[0x0003u] = 0x01u; // ADD A,0x01
        memory[0x0004u] = 0xCEu; memory[0x0005u] = 0xFFu; // ADC A,0xFF
        memory[0x0006u] = 0xD6u; memory[0x0007u] = 0x80u; // SUB 0x80
        memory[0x0008u] = 0xDEu; memory[0x0009u] = 0x00u; // SBC A,0x00

        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x80u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagS | kFlagH | kFlagPV));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x7Fu);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagPV | kFlagC));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0xFFu);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagS | kFlagN | kFlagPV | kFlagC));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0xFEu);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagS | kFlagN));
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        bool interruptPending = true;
        cpu.setInterruptRequestProvider([&interruptPending]() -> std::optional<uint8_t> {
            if (!interruptPending) {
                return std::nullopt;
            }
            interruptPending = false;
            return 0xAAu;
        });
        cpu.setInterruptMode(9u);
        cpu.IME = true;
        cpu.IFF1 = true;
        cpu.SP = 0xD000u;
        cpu.PC = 0x1234u;

        assert(cpu.step() == 11u);
        assert(cpu.PC == 0x0038u);
        assert(cpu.SP == 0xCFFEu);
        assert(memory[0xCFFFu] == 0x12u);
        assert(memory[0xCFFEu] == 0x34u);
        assert(!cpu.IME);
        assert(!cpu.IFF1);
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0x01u; memory[0x0001u] = 0x34u; memory[0x0002u] = 0x12u; // LD BC,0x1234
        memory[0x0003u] = 0x11u; memory[0x0004u] = 0x78u; memory[0x0005u] = 0x56u; // LD DE,0x5678
        memory[0x0006u] = 0x03u;                                                   // INC BC
        memory[0x0007u] = 0x13u;                                                   // INC DE
        memory[0x0008u] = 0x0Bu;                                                   // DEC BC
        memory[0x0009u] = 0x1Bu;                                                   // DEC DE
        memory[0x000Au] = 0x21u; memory[0x000Bu] = 0x00u; memory[0x000Cu] = 0x20u; // LD HL,0x2000
        memory[0x000Du] = 0x22u; memory[0x000Eu] = 0x00u; memory[0x000Fu] = 0x40u; // LD (0x4000),HL
        memory[0x0010u] = 0x2Au; memory[0x0011u] = 0x00u; memory[0x0012u] = 0x40u; // LD HL,(0x4000)
        memory[0x0013u] = 0x31u; memory[0x0014u] = 0x00u; memory[0x0015u] = 0xD0u; // LD SP,0xD000
        memory[0x0016u] = 0x33u;                                                   // INC SP
        memory[0x0017u] = 0x3Bu;                                                   // DEC SP

        assert(cpu.step() == 10u);
        assert(cpu.step() == 10u);
        assert(cpu.step() == 6u);
        assert(cpu.BC == 0x1235u);
        assert(cpu.step() == 6u);
        assert(cpu.DE == 0x5679u);
        assert(cpu.step() == 6u);
        assert(cpu.BC == 0x1234u);
        assert(cpu.step() == 6u);
        assert(cpu.DE == 0x5678u);
        assert(cpu.step() == 10u);
        assert(cpu.step() == 16u);
        assert(memory[0x4000u] == 0x00u);
        assert(memory[0x4001u] == 0x20u);
        cpu.HL = 0u;
        assert(cpu.step() == 16u);
        assert(cpu.HL == 0x2000u);
        assert(cpu.step() == 10u);
        assert(cpu.step() == 6u);
        assert(cpu.SP == 0xD001u);
        assert(cpu.step() == 6u);
        assert(cpu.SP == 0xD000u);
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0x3Eu; memory[0x0001u] = 0x00u; // LD A,0
        memory[0x0002u] = 0xFEu; memory[0x0003u] = 0x00u; // CP 0
        memory[0x0004u] = 0x28u; memory[0x0005u] = 0x02u; // JR Z,+2
        memory[0x0006u] = 0x3Eu; memory[0x0007u] = 0x11u; // skipped
        memory[0x0008u] = 0x3Eu; memory[0x0009u] = 0x22u; // LD A,0x22
        memory[0x000Au] = 0xC2u; memory[0x000Bu] = 0x10u; memory[0x000Cu] = 0x00u; // JP NZ,0x0010 (not taken)
        memory[0x000Du] = 0xD6u; memory[0x000Eu] = 0x23u; // SUB 0x23 sets carry
        memory[0x000Fu] = 0x38u; memory[0x0010u] = 0x02u; // JR C,+2
        memory[0x0011u] = 0x3Eu; memory[0x0012u] = 0x33u; // skipped
        memory[0x0013u] = 0x30u; memory[0x0014u] = 0x02u; // JR NC,+2 (not taken)
        memory[0x0015u] = 0x3Eu; memory[0x0016u] = 0x44u; // executed

        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(cpu.step() == 12u);
        assert(cpu.PC == 0x0008u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x22u);
        assert(cpu.step() == 10u);
        assert(cpu.PC == 0x000Du);
        assert(cpu.step() == 7u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagS | kFlagN | kFlagH | kFlagC));
        assert(cpu.step() == 12u);
        assert(cpu.PC == 0x0013u);
        assert(cpu.step() == 7u);
        assert(cpu.PC == 0x0015u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x44u);
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        cpu.SP = 0xD000u;
        cpu.AF = 0x1234u;
        cpu.AF_ = 0xAB01u;
        cpu.BC = 0x1111u;
        cpu.DE = 0x2222u;
        cpu.HL = 0x3333u;
        cpu.BC_ = 0xAAAAu;
        cpu.DE_ = 0xBBBBu;
        cpu.HL_ = 0xCCCCu;

        memory[0x0000u] = 0x08u; // EX AF,AF'
        memory[0x0001u] = 0xD9u; // EXX
        memory[0x0002u] = 0xC4u; memory[0x0003u] = 0x08u; memory[0x0004u] = 0x00u; // CALL NZ,0x0008
        memory[0x0005u] = 0xCCu; memory[0x0006u] = 0x08u; memory[0x0007u] = 0x00u; // CALL Z,0x0008 (not taken)
        memory[0x0008u] = 0xAFu; // XOR A -> Z set
        memory[0x0009u] = 0xC8u; // RET Z

        assert(cpu.step() == 4u);
        assert(cpu.AF == 0xAB01u);
        assert(cpu.AF_ == 0x1234u);
        assert(cpu.step() == 4u);
        assert(cpu.BC == 0xAAAAu);
        assert(cpu.DE == 0xBBBBu);
        assert(cpu.HL == 0xCCCCu);
        assert(cpu.step() == 17u);
        assert(cpu.PC == 0x0008u);
        assert(cpu.SP == 0xCFFEu);
        assert(memory[0xCFFEu] == 0x05u);
        assert(memory[0xCFFFu] == 0x00u);
        assert(cpu.step() == 4u);
        assert((cpu.AF & 0x00FFu) == static_cast<uint16_t>(kFlagZ | kFlagPV));
        assert(cpu.step() == 11u);
        assert(cpu.PC == 0x0005u);
        assert(cpu.SP == 0xD000u);
        assert(cpu.step() == 17u);
        assert(cpu.PC == 0x0008u);
    }

    return 0;
}
