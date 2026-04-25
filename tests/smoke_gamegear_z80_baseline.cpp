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
constexpr uint8_t kDocumentedFlagMask = kFlagS | kFlagZ | kFlagH | kFlagPV | kFlagN | kFlagC;

Z80Interpreter makeCpu(std::vector<uint8_t>& memory) {
    Z80Interpreter cpu;
    cpu.setMemoryInterface(
        [&](uint16_t address) { return memory[address]; },
        [&](uint16_t address, uint8_t value) { memory[address] = value; }
    );
    cpu.reset();
    return cpu;
}

template <typename IoRead, typename IoWrite>
Z80Interpreter makeCpu(std::vector<uint8_t>& memory, IoRead&& ioRead, IoWrite&& ioWrite) {
    Z80Interpreter cpu;
    cpu.setMemoryInterface(
        [&](uint16_t address) { return memory[address]; },
        [&](uint16_t address, uint8_t value) { memory[address] = value; }
    );
    cpu.setIoInterface(std::forward<IoRead>(ioRead), std::forward<IoWrite>(ioWrite));
    cpu.reset();
    return cpu;
}

#define CHECK_OR_FAIL(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "smoke_gamegear_z80_baseline: " << message << '\n'; \
            return 1; \
        } \
    } while (false)

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
        assert((cpu.AF & kDocumentedFlagMask) == 0x00u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x15u);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagN | kFlagZ));
        assert(cpu.step() == 7u);
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x00u);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagZ | kFlagH | kFlagPV));
        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x80u);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagS));
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
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagS | kFlagH | kFlagPV));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0x7Fu);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagPV | kFlagC));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0xFFu);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagS | kFlagN | kFlagPV | kFlagC));

        assert(cpu.step() == 7u);
        assert(static_cast<uint8_t>(cpu.AF >> 8u) == 0xFEu);
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagS | kFlagN));
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
        std::optional<uint8_t> lastPortRead;
        std::vector<std::pair<uint8_t, uint8_t>> portWrites;
        auto cpu = makeCpu(
            memory,
            [&](uint8_t port) {
                lastPortRead = port;
                return static_cast<uint8_t>(0xA5u);
            },
            [&](uint8_t port, uint8_t value) {
                portWrites.emplace_back(port, value);
            });
        memory[0x0000u] = 0x01u; memory[0x0001u] = 0xBFu; memory[0x0002u] = 0x12u; // LD BC,0x12BF
        memory[0x0003u] = 0x11u; memory[0x0004u] = 0x78u; memory[0x0005u] = 0x56u; // LD DE,0x5678
        memory[0x0006u] = 0x21u; memory[0x0007u] = 0x00u; memory[0x0008u] = 0x40u; // LD HL,0x4000
        memory[0x0009u] = 0x31u; memory[0x000Au] = 0x00u; memory[0x000Bu] = 0xD0u; // LD SP,0xD000
        memory[0x000Cu] = 0x3Eu; memory[0x000Du] = 0x9Au;                           // LD A,0x9A
        memory[0x000Eu] = 0xEDu; memory[0x000Fu] = 0x79u;                           // OUT (C),A
        memory[0x0010u] = 0xEDu; memory[0x0011u] = 0x41u;                           // OUT (C),B
        memory[0x0012u] = 0xEDu; memory[0x0013u] = 0x61u;                           // OUT (C),H
        memory[0x0014u] = 0xEDu; memory[0x0015u] = 0x69u;                           // OUT (C),L
        memory[0x0016u] = 0xEDu; memory[0x0017u] = 0x78u;                           // IN A,(C)
        memory[0x0018u] = 0xEDu; memory[0x0019u] = 0x43u; memory[0x001Au] = 0x20u; memory[0x001Bu] = 0x40u; // LD (0x4020),BC
        memory[0x001Cu] = 0xEDu; memory[0x001Du] = 0x53u; memory[0x001Eu] = 0x22u; memory[0x001Fu] = 0x40u; // LD (0x4022),DE
        memory[0x0020u] = 0xEDu; memory[0x0021u] = 0x73u; memory[0x0022u] = 0x24u; memory[0x0023u] = 0x40u; // LD (0x4024),SP
        memory[0x0024u] = 0x01u; memory[0x0025u] = 0x00u; memory[0x0026u] = 0x00u; // LD BC,0x0000
        memory[0x0027u] = 0x11u; memory[0x0028u] = 0x00u; memory[0x0029u] = 0x00u; // LD DE,0x0000
        memory[0x002Au] = 0x31u; memory[0x002Bu] = 0x00u; memory[0x002Cu] = 0x00u; // LD SP,0x0000
        memory[0x002Du] = 0xEDu; memory[0x002Eu] = 0x4Bu; memory[0x002Fu] = 0x20u; memory[0x0030u] = 0x40u; // LD BC,(0x4020)
        memory[0x0031u] = 0xEDu; memory[0x0032u] = 0x5Bu; memory[0x0033u] = 0x22u; memory[0x0034u] = 0x40u; // LD DE,(0x4022)
        memory[0x0035u] = 0xEDu; memory[0x0036u] = 0x7Bu; memory[0x0037u] = 0x24u; memory[0x0038u] = 0x40u; // LD SP,(0x4024)
        memory[0x0039u] = 0x01u; memory[0x003Au] = 0xBEu; memory[0x003Bu] = 0x03u; // LD BC,0x03BE
        memory[0x003Cu] = 0x21u; memory[0x003Du] = 0x30u; memory[0x003Eu] = 0x40u; // LD HL,0x4030
        memory[0x003Fu] = 0xEDu; memory[0x0040u] = 0xB3u;                           // OTIR
        memory[0x4030u] = 0x11u;
        memory[0x4031u] = 0x22u;
        memory[0x4032u] = 0x33u;

        CHECK_OR_FAIL(cpu.step() == 10u, "ED regression setup failed at LD BC");
        CHECK_OR_FAIL(cpu.step() == 10u, "ED regression setup failed at LD DE");
        CHECK_OR_FAIL(cpu.step() == 10u, "ED regression setup failed at LD HL");
        CHECK_OR_FAIL(cpu.step() == 10u, "ED regression setup failed at LD SP");
        CHECK_OR_FAIL(cpu.step() == 7u, "ED regression setup failed at LD A");
        CHECK_OR_FAIL(cpu.step() == 12u, "OUT (C),A missing");
        CHECK_OR_FAIL(cpu.step() == 12u, "OUT (C),B missing");
        CHECK_OR_FAIL(cpu.step() == 12u, "OUT (C),H missing");
        CHECK_OR_FAIL(cpu.step() == 12u, "OUT (C),L missing");
        CHECK_OR_FAIL(portWrites.size() == 4u, "expected four ED OUT writes");
        CHECK_OR_FAIL(portWrites[0] == std::make_pair(static_cast<uint8_t>(0xBFu), static_cast<uint8_t>(0x9Au)),
                      "OUT (C),A wrote wrong value");
        CHECK_OR_FAIL(portWrites[1] == std::make_pair(static_cast<uint8_t>(0xBFu), static_cast<uint8_t>(0x12u)),
                      "OUT (C),B wrote wrong value");
        CHECK_OR_FAIL(portWrites[2] == std::make_pair(static_cast<uint8_t>(0xBFu), static_cast<uint8_t>(0x40u)),
                      "OUT (C),H wrote wrong value");
        CHECK_OR_FAIL(portWrites[3] == std::make_pair(static_cast<uint8_t>(0xBFu), static_cast<uint8_t>(0x00u)),
                      "OUT (C),L wrote wrong value");
        CHECK_OR_FAIL(cpu.step() == 12u, "IN A,(C) missing");
        CHECK_OR_FAIL(lastPortRead.has_value() && *lastPortRead == 0xBFu, "IN A,(C) read wrong port");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0xA5u, "IN A,(C) read wrong value");
        CHECK_OR_FAIL(cpu.step() == 20u, "LD (nn),BC missing");
        CHECK_OR_FAIL(memory[0x4020u] == 0xBFu && memory[0x4021u] == 0x12u, "LD (nn),BC wrote wrong bytes");
        CHECK_OR_FAIL(cpu.step() == 20u, "LD (nn),DE missing");
        CHECK_OR_FAIL(memory[0x4022u] == 0x78u && memory[0x4023u] == 0x56u, "LD (nn),DE wrote wrong bytes");
        CHECK_OR_FAIL(cpu.step() == 20u, "LD (nn),SP missing");
        CHECK_OR_FAIL(memory[0x4024u] == 0x00u && memory[0x4025u] == 0xD0u, "LD (nn),SP wrote wrong bytes");
        CHECK_OR_FAIL(cpu.step() == 10u, "reset BC for ED load failed");
        CHECK_OR_FAIL(cpu.step() == 10u, "reset DE for ED load failed");
        CHECK_OR_FAIL(cpu.step() == 10u, "reset SP for ED load failed");
        CHECK_OR_FAIL(cpu.step() == 20u && cpu.BC == 0x12BFu, "LD BC,(nn) missing");
        CHECK_OR_FAIL(cpu.step() == 20u && cpu.DE == 0x5678u, "LD DE,(nn) missing");
        CHECK_OR_FAIL(cpu.step() == 20u && cpu.SP == 0xD000u, "LD SP,(nn) missing");
        CHECK_OR_FAIL(cpu.step() == 10u, "OTIR setup failed at LD BC");
        CHECK_OR_FAIL(cpu.step() == 10u, "OTIR setup failed at LD HL");
        CHECK_OR_FAIL(cpu.step() == 58u, "OTIR missing");
        CHECK_OR_FAIL(cpu.BC == 0x00BEu, "OTIR should decrement B to zero and preserve port");
        CHECK_OR_FAIL(cpu.HL == 0x4033u, "OTIR should advance HL across copied bytes");
        CHECK_OR_FAIL(portWrites.size() == 7u, "OTIR should emit three port writes");
        CHECK_OR_FAIL(portWrites[4] == std::make_pair(static_cast<uint8_t>(0xBEu), static_cast<uint8_t>(0x11u)),
                      "OTIR wrote wrong first byte");
        CHECK_OR_FAIL(portWrites[5] == std::make_pair(static_cast<uint8_t>(0xBEu), static_cast<uint8_t>(0x22u)),
                      "OTIR wrote wrong second byte");
        CHECK_OR_FAIL(portWrites[6] == std::make_pair(static_cast<uint8_t>(0xBEu), static_cast<uint8_t>(0x33u)),
                      "OTIR wrote wrong third byte");
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x0000u] = 0xDDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x34u; memory[0x0003u] = 0x12u; // LD IX,0x1234
        memory[0x0004u] = 0xFDu; memory[0x0005u] = 0x21u; memory[0x0006u] = 0x78u; memory[0x0007u] = 0x56u; // LD IY,0x5678
        memory[0x0008u] = 0xDDu; memory[0x0009u] = 0x22u; memory[0x000Au] = 0x20u; memory[0x000Bu] = 0x40u; // LD (0x4020),IX
        memory[0x000Cu] = 0xFDu; memory[0x000Du] = 0x22u; memory[0x000Eu] = 0x22u; memory[0x000Fu] = 0x40u; // LD (0x4022),IY
        memory[0x0010u] = 0x3Eu; memory[0x0011u] = 0x9Au;                                                   // LD A,0x9A
        memory[0x0012u] = 0xDDu; memory[0x0013u] = 0x32u; memory[0x0014u] = 0x24u; memory[0x0015u] = 0x40u; // DD prefix should preserve LD (nn),A

        CHECK_OR_FAIL(cpu.step() == 14u && cpu.IX == 0x1234u, "LD IX,nn missing");
        CHECK_OR_FAIL(cpu.step() == 14u && cpu.IY == 0x5678u, "LD IY,nn missing");
        CHECK_OR_FAIL(cpu.step() == 20u, "LD (nn),IX missing");
        CHECK_OR_FAIL(memory[0x4020u] == 0x34u && memory[0x4021u] == 0x12u, "LD (nn),IX wrote wrong bytes");
        CHECK_OR_FAIL(cpu.step() == 20u, "LD (nn),IY missing");
        CHECK_OR_FAIL(memory[0x4022u] == 0x78u && memory[0x4023u] == 0x56u, "LD (nn),IY wrote wrong bytes");
        CHECK_OR_FAIL(cpu.step() == 7u, "IX/IY prefix regression setup failed at LD A");
        CHECK_OR_FAIL(cpu.step() == 13u, "DD passthrough should preserve LD (nn),A");
        CHECK_OR_FAIL(memory[0x4024u] == 0x9Au, "DD passthrough desynchronized the stream");
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
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagS | kFlagN | kFlagH | kFlagC));
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
        cpu.PC = 0x0149u;
        memory[0x0149u] = 0x18u; memory[0x014Au] = 0xE2u; // JR -30

        CHECK_OR_FAIL(cpu.step() == 12u, "unconditional JR -30 should take 12 cycles");
        CHECK_OR_FAIL(cpu.PC == 0x012Du, "unconditional JR should branch relative to the post-operand PC");
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
        assert((cpu.AF & kDocumentedFlagMask) == static_cast<uint16_t>(kFlagZ | kFlagPV));
        assert(cpu.step() == 11u);
        assert(cpu.PC == 0x0005u);
        assert(cpu.SP == 0xD000u);
        assert(cpu.step() == 17u);
        assert(cpu.PC == 0x0008u);
    }

    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);
        memory[0x4000u] = 0x11u;
        memory[0x4001u] = 0x22u;
        memory[0x4002u] = 0x33u;
        memory[0x5000u] = 0x00u;
        memory[0x5001u] = 0x00u;
        memory[0x5002u] = 0x00u;
        memory[0x6000u] = 0x34u;
        memory[0x6005u] = 0x81u;
        memory[0x0000u] = 0x21u; memory[0x0001u] = 0x00u; memory[0x0002u] = 0x40u; // LD HL,0x4000
        memory[0x0003u] = 0x11u; memory[0x0004u] = 0x00u; memory[0x0005u] = 0x50u; // LD DE,0x5000
        memory[0x0006u] = 0x01u; memory[0x0007u] = 0x03u; memory[0x0008u] = 0x00u; // LD BC,3
        memory[0x0009u] = 0xEDu; memory[0x000Au] = 0xB0u;                           // LDIR
        memory[0x000Bu] = 0x3Eu; memory[0x000Cu] = 0x15u;                           // LD A,0x15
        memory[0x000Du] = 0xC6u; memory[0x000Eu] = 0x27u;                           // ADD A,0x27
        memory[0x000Fu] = 0x27u;                                                    // DAA -> 0x42
        memory[0x0010u] = 0x07u;                                                    // RLCA -> 0x84
        memory[0x0011u] = 0x17u;                                                    // RLA -> 0x08, C=1
        memory[0x0012u] = 0xDDu; memory[0x0013u] = 0x21u; memory[0x0014u] = 0x00u; memory[0x0015u] = 0x60u; // LD IX,0x6000
        memory[0x0016u] = 0xDDu; memory[0x0017u] = 0x7Eu; memory[0x0018u] = 0x05u;  // LD A,(IX+5)
        memory[0x0019u] = 0xDDu; memory[0x001Au] = 0xCBu; memory[0x001Bu] = 0x05u; memory[0x001Cu] = 0x00u; // RLC (IX+5),B
        memory[0x001Du] = 0x3Eu; memory[0x001Eu] = 0xA1u;                           // LD A,0xA1
        memory[0x001Fu] = 0xEDu; memory[0x0020u] = 0x67u;                           // RRD
        memory[0x0021u] = 0xEDu; memory[0x0022u] = 0x6Fu;                           // RLD
        memory[0x0023u] = 0x21u; memory[0x0024u] = 0x00u; memory[0x0025u] = 0x50u; // LD HL,0x5000
        memory[0x0026u] = 0x01u; memory[0x0027u] = 0x03u; memory[0x0028u] = 0x00u; // LD BC,3
        memory[0x0029u] = 0x3Eu; memory[0x002Au] = 0x33u;                           // LD A,0x33
        memory[0x002Bu] = 0xEDu; memory[0x002Cu] = 0xB1u;                           // CPIR

        CHECK_OR_FAIL(cpu.step() == 10u, "LD HL setup for LDIR failed");
        CHECK_OR_FAIL(cpu.step() == 10u, "LD DE setup for LDIR failed");
        CHECK_OR_FAIL(cpu.step() == 10u, "LD BC setup for LDIR failed");
        CHECK_OR_FAIL(cpu.step() == 58u, "LDIR should transfer all bytes in one interpreter step");
        CHECK_OR_FAIL(memory[0x5000u] == 0x11u && memory[0x5001u] == 0x22u && memory[0x5002u] == 0x33u,
                      "LDIR did not copy the full range");
        CHECK_OR_FAIL(cpu.BC == 0u && cpu.HL == 0x4003u && cpu.DE == 0x5003u,
                      "LDIR did not update BC/HL/DE correctly");
        CHECK_OR_FAIL(cpu.step() == 7u, "DAA setup LD A failed");
        CHECK_OR_FAIL(cpu.step() == 7u, "DAA setup ADD failed");
        CHECK_OR_FAIL(cpu.step() == 4u, "DAA opcode missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0x42u, "DAA produced wrong BCD result");
        CHECK_OR_FAIL(cpu.step() == 4u, "RLCA opcode missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0x84u, "RLCA produced wrong result");
        CHECK_OR_FAIL(cpu.step() == 4u, "RLA opcode missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0x08u && (cpu.AF & kFlagC) != 0u, "RLA produced wrong result/carry");
        CHECK_OR_FAIL(cpu.step() == 14u, "LD IX,nn missing");
        CHECK_OR_FAIL(cpu.step() == 19u, "LD A,(IX+d) missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0x81u, "LD A,(IX+d) read wrong byte");
        CHECK_OR_FAIL(cpu.step() == 23u, "DDCB RLC (IX+d),B missing");
        CHECK_OR_FAIL(memory[0x6005u] == 0x03u && static_cast<uint8_t>(cpu.BC >> 8u) == 0x03u,
                      "DDCB RLC should update memory and destination register");
        cpu.HL = 0x6000u;
        CHECK_OR_FAIL(cpu.step() == 7u, "RLD/RRD setup LD A failed");
        CHECK_OR_FAIL(cpu.step() == 18u, "RRD missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0xA4u && memory[0x6000u] == 0x13u, "RRD result mismatch");
        CHECK_OR_FAIL(cpu.step() == 18u, "RLD missing");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0xA1u && memory[0x6000u] == 0x34u, "RLD result mismatch");
        CHECK_OR_FAIL(cpu.step() == 10u, "CPIR setup LD HL failed");
        CHECK_OR_FAIL(cpu.step() == 10u, "CPIR setup LD BC failed");
        CHECK_OR_FAIL(cpu.step() == 7u, "CPIR setup LD A failed");
        CHECK_OR_FAIL(cpu.step() == 58u, "CPIR should search until match in one interpreter step");
        CHECK_OR_FAIL(cpu.BC == 0u && cpu.HL == 0x5003u && (cpu.AF & kFlagZ) != 0u,
                      "CPIR did not stop with the expected match state");
    }

    return 0;
}
