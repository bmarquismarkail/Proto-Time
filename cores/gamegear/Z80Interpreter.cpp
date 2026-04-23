// Helper for INC flag calculation (preserves C, computes Z, S, H, PV)
uint8_t Z80Interpreter::computeIncFlags(uint8_t before, uint8_t result) const noexcept {
    uint8_t flags = regF() & kFlagC;
    if (result == 0u) flags |= kFlagZ;
    if ((result & 0x80u) != 0u) flags |= kFlagS;
    if (((before & 0x0Fu) + 1u) > 0x0Fu) flags |= kFlagH;
    if (before == 0x7Fu) flags |= kFlagPV;
    return flags;
}
#include "Z80Interpreter.hpp"
#include <stdexcept>

Z80Interpreter::Z80Interpreter() {}

Z80Interpreter::~Z80Interpreter() {}

void Z80Interpreter::setMemoryInterface(MemRead reader, MemWrite writer) {
    memRead = std::move(reader);
    memWrite = std::move(writer);
}

void Z80Interpreter::setIoInterface(IoRead reader, IoWrite writer) {
    ioRead = std::move(reader);
    ioWrite = std::move(writer);
}

void Z80Interpreter::requireMemoryInterface() const {
    if (!memRead || !memWrite) {
        throw std::runtime_error("Z80 memory interface not set");
    }
}

void Z80Interpreter::setInterruptRequestProvider(std::function<std::optional<uint8_t>()> provider) {
    interruptRequestProvider = std::move(provider);
}

uint8_t Z80Interpreter::readIo(uint8_t port) const {
    return ioRead ? ioRead(port) : 0xFFu;
}

void Z80Interpreter::writeIo(uint8_t port, uint8_t value) const {
    if (ioWrite) {
        ioWrite(port, value);
    }
}

void Z80Interpreter::reset() {
    AF = 0x01B0; // Game Gear boot values (similar to SMS)
    BC = 0x0000;
    DE = 0x0000;
    HL = 0x0000;
    IX = 0x0000;
    IY = 0x0000;
    SP = 0xDFF0;
    PC = 0x0000;
    AF_ = BC_ = DE_ = HL_ = 0;
    I = 0;
    R = 0;
    IFF1 = IFF2 = IME = false;
    imeEnableDelay_ = 0;
}

uint8_t Z80Interpreter::fetch8() {
    requireMemoryInterface();
    const uint8_t val = memRead(PC);
    PC = (PC + 1) & 0xFFFF;
    return val;
}

uint16_t Z80Interpreter::fetch16() {
    uint16_t lo = fetch8();
    uint16_t hi = fetch8();
    return lo | (hi << 8);
}

uint8_t Z80Interpreter::regA() const noexcept {
    return static_cast<uint8_t>((AF >> 8) & 0x00FFu);
}

void Z80Interpreter::setRegA(uint8_t value) noexcept {
    AF = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | (AF & 0x00FFu));
}

uint8_t Z80Interpreter::regF() const noexcept {
    return static_cast<uint8_t>(AF & 0x00FFu);
}

void Z80Interpreter::setRegF(uint8_t value) noexcept {
    AF = static_cast<uint16_t>((AF & 0xFF00u) | value);
}

void Z80Interpreter::setZeroFlag(bool set) noexcept {
    auto flags = static_cast<uint8_t>(regF() & static_cast<uint8_t>(~kFlagZ));
    if (set) {
        flags = static_cast<uint8_t>(flags | kFlagZ);
    }
    setRegF(flags);
}

bool Z80Interpreter::zeroFlag() const noexcept {
    return (regF() & kFlagZ) != 0u;
}

uint32_t Z80Interpreter::executeOpcode(uint8_t opcode) {
    switch (opcode) {
        case 0x00: // NOP
            return 4u;
        case 0x06: { // LD B,n
            const uint8_t value = fetch8();
            BC = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | (BC & 0x00FFu));
            return 7u;
        }
        case 0x0E: { // LD C,n
            const uint8_t value = fetch8();
            BC = static_cast<uint16_t>((BC & 0xFF00u) | value);
            return 7u;
        }
        case 0x10: { // DJNZ e
            const int8_t displacement = static_cast<int8_t>(fetch8());
            auto b = static_cast<uint8_t>((BC >> 8) & 0x00FFu);
            b = static_cast<uint8_t>(b - 1u);
            BC = static_cast<uint16_t>((static_cast<uint16_t>(b) << 8) | (BC & 0x00FFu));
            if (b != 0u) {
                PC = static_cast<uint16_t>(PC + displacement);
                return 13u;
            }
            return 8u;
        }
        case 0x18: { // JR e
            const int8_t displacement = static_cast<int8_t>(fetch8());
            PC = static_cast<uint16_t>(PC + displacement);
            return 12u;
        }
        case 0x20: { // JR NZ,e
            const int8_t displacement = static_cast<int8_t>(fetch8());
            if (!zeroFlag()) {
                PC = static_cast<uint16_t>(PC + displacement);
            }
            return 12u;
        }
        case 0x21: // LD HL,nn
            HL = fetch16();
            return 10u;
        case 0x23: // INC HL
            HL = static_cast<uint16_t>(HL + 1u);
            return 6u;
        case 0x3C: { // INC A
            const auto before = regA();
            const auto result = static_cast<uint8_t>(before + 1u);
            setRegA(result);
            uint8_t newFlags = static_cast<uint8_t>(regF() & kFlagC);
            if (result == 0u) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            }
            if ((result & 0x80u) != 0u) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            }
            if (((before & 0x0Fu) + 1u) > 0x0Fu) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            }
            if (before == 0x7Fu) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            }
            setRegF(newFlags);
            return 4u;
        }
        case 0x36: { // LD (HL),n
            const uint8_t value = fetch8();
            memWrite(HL, value);
            return 10u;
        }
        case 0x3E: { // LD A,n
            const uint8_t value = fetch8();
            setRegA(value);
            return 7u;
        }
        case 0x04: { // INC B
            uint8_t before = static_cast<uint8_t>((BC >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            BC = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (BC & 0x00FFu));
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x0C: { // INC C
            uint8_t before = static_cast<uint8_t>(BC & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            BC = static_cast<uint16_t>((BC & 0xFF00u) | result);
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x14: { // INC D
            uint8_t before = static_cast<uint8_t>((DE >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            DE = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (DE & 0x00FFu));
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x1C: { // INC E
            uint8_t before = static_cast<uint8_t>(DE & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            DE = static_cast<uint16_t>((DE & 0xFF00u) | result);
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x24: { // INC H
            uint8_t before = static_cast<uint8_t>((HL >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            HL = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (HL & 0x00FFu));
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x2C: { // INC L
            uint8_t before = static_cast<uint8_t>(HL & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            HL = static_cast<uint16_t>((HL & 0xFF00u) | result);
            setRegF(computeIncFlags(before, result));
            return 4u;
        }
        case 0x34: { // INC (HL)
            uint8_t before = memRead(HL);
            uint8_t result = static_cast<uint8_t>(before + 1u);
            memWrite(HL, result);
            setRegF(computeIncFlags(before, result));
            return 11u;
        }
        case 0x05: { // DEC B
            uint8_t before = static_cast<uint8_t>((BC >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            BC = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (BC & 0x00FFu));
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x0D: { // DEC C
            uint8_t before = static_cast<uint8_t>(BC & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            BC = static_cast<uint16_t>((BC & 0xFF00u) | result);
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x15: { // DEC D
            uint8_t before = static_cast<uint8_t>((DE >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            DE = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (DE & 0x00FFu));
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x1D: { // DEC E
            uint8_t before = static_cast<uint8_t>(DE & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            DE = static_cast<uint16_t>((DE & 0xFF00u) | result);
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x25: { // DEC H
            uint8_t before = static_cast<uint8_t>((HL >> 8) & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            HL = static_cast<uint16_t>((static_cast<uint16_t>(result) << 8) | (HL & 0x00FFu));
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x2D: { // DEC L
            uint8_t before = static_cast<uint8_t>(HL & 0x00FFu);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            HL = static_cast<uint16_t>((HL & 0xFF00u) | result);
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 4u;
        }
        case 0x35: { // DEC (HL)
            uint8_t before = memRead(HL);
            uint8_t result = static_cast<uint8_t>(before - 1u);
            memWrite(HL, result);
            uint8_t preserved = static_cast<uint8_t>(regF() & kFlagC);
            uint8_t newFlags = static_cast<uint8_t>(preserved);
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            if ((before & 0x0Fu) == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            if (before == 0x80u) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            newFlags = static_cast<uint8_t>(newFlags | kFlagN);
            setRegF(newFlags);
            return 11u;
        }
        case 0x77: { // LD (HL),A
            const uint8_t value = regA();
            memWrite(HL, value);
            return 7u;
        }
        case 0x7E: { // LD A,(HL)
            const uint8_t value = memRead(HL);
            setRegA(value);
            return 7u;
        }
        case 0x32: { // LD (nn),A
            const uint16_t addr = fetch16();
            memWrite(addr, regA());
            return 13u;
        }
        case 0x3A: { // LD A,(nn)
            const uint16_t addr = fetch16();
            const uint8_t value = memRead(addr);
            setRegA(value);
            return 13u;
        }
        case 0xEE: { // XOR n (immediate)
            const uint8_t value = fetch8();
            const uint8_t result = static_cast<uint8_t>(regA() ^ value);
            setRegA(result);
            uint8_t newFlags = 0u;
            if (result == 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            if ((result & 0x80u) != 0u) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            // Parity
            {
                int ones = 0;
                uint8_t tmp = result;
                while (tmp) { ones += tmp & 1u; tmp >>= 1; }
                if ((ones & 1) == 0) newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
            }
            setRegF(newFlags);
            return 7u;
        }
        case 0xC5: { // PUSH BC
            const uint8_t bh = static_cast<uint8_t>((BC >> 8) & 0x00FFu);
            const uint8_t bl = static_cast<uint8_t>(BC & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, bh);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, bl);
            return 11u;
        }
        case 0xD5: { // PUSH DE
            const uint8_t dh = static_cast<uint8_t>((DE >> 8) & 0x00FFu);
            const uint8_t dl = static_cast<uint8_t>(DE & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, dh);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, dl);
            return 11u;
        }
        case 0xE5: { // PUSH HL
            const uint8_t hh = static_cast<uint8_t>((HL >> 8) & 0x00FFu);
            const uint8_t hlb = static_cast<uint8_t>(HL & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, hh);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, hlb);
            return 11u;
        }
        case 0xF5: { // PUSH AF
            const uint8_t ah = static_cast<uint8_t>((AF >> 8) & 0x00FFu);
            const uint8_t al = static_cast<uint8_t>(AF & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, ah);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, al);
            return 11u;
        }
        case 0xC1: { // POP BC
            const uint8_t regLo = memRead(SP);
            const uint8_t regHi = memRead(static_cast<uint16_t>(SP + 1u));
            SP = static_cast<uint16_t>(SP + 2u);
            BC = static_cast<uint16_t>(static_cast<uint16_t>(regLo) | (static_cast<uint16_t>(regHi) << 8u));
            return 10u;
        }
        case 0xD1: { // POP DE
            const uint8_t regLo = memRead(SP);
            const uint8_t regHi = memRead(static_cast<uint16_t>(SP + 1u));
            SP = static_cast<uint16_t>(SP + 2u);
            DE = static_cast<uint16_t>(static_cast<uint16_t>(regLo) | (static_cast<uint16_t>(regHi) << 8u));
            return 10u;
        }
        case 0xE1: { // POP HL
            const uint8_t regLo = memRead(SP);
            const uint8_t regHi = memRead(static_cast<uint16_t>(SP + 1u));
            SP = static_cast<uint16_t>(SP + 2u);
            HL = static_cast<uint16_t>(static_cast<uint16_t>(regLo) | (static_cast<uint16_t>(regHi) << 8u));
            return 10u;
        }
        case 0xF1: { // POP AF
            const uint8_t regLo = memRead(SP);
            const uint8_t regHi = memRead(static_cast<uint16_t>(SP + 1u));
            SP = static_cast<uint16_t>(SP + 2u);
            AF = static_cast<uint16_t>(static_cast<uint16_t>(regLo) | (static_cast<uint16_t>(regHi) << 8u));
            return 10u;
        }
        case 0xCD: { // CALL nn
            const uint16_t addr = fetch16();
            const uint8_t pcl = static_cast<uint8_t>(PC & 0x00FFu);
            const uint8_t pch = static_cast<uint8_t>((PC >> 8) & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, pch);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, pcl);
            PC = addr;
            return 17u;
        }
        case 0xC9: { // RET
            const uint8_t pcl = memRead(SP);
            const uint8_t pch = memRead(static_cast<uint16_t>(SP + 1u));
            SP = static_cast<uint16_t>(SP + 2u);
            PC = static_cast<uint16_t>(static_cast<uint16_t>(pcl) | (static_cast<uint16_t>(pch) << 8u));
            return 10u;
        }
        case 0xC7: // RST 0x00
        case 0xCF: // RST 0x08
        case 0xD7: // RST 0x10
        case 0xDF: // RST 0x18
        case 0xE7: // RST 0x20
        case 0xEF: // RST 0x28
        case 0xF7: // RST 0x30
        case 0xFF: { // RST 0x38
            const uint8_t pcl = static_cast<uint8_t>(PC & 0x00FFu);
            const uint8_t pch = static_cast<uint8_t>((PC >> 8) & 0x00FFu);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, pch);
            SP = static_cast<uint16_t>(SP - 1u);
            memWrite(SP, pcl);
            // Compute vector directly from opcode bits (RST mapping)
            uint16_t vector = static_cast<uint16_t>(opcode & 0x38);
            PC = vector;
            return 11u;
        }
        case 0xAF: { // XOR A
            setRegA(0u);
            setRegF(static_cast<uint8_t>(kFlagZ | kFlagPV));
            return 4u;
        }
        case 0xC3: // JP nn
            PC = fetch16();
            return 10u;
        case 0xCA: { // JP Z,nn
            const uint16_t address = fetch16();
            if (zeroFlag()) {
                PC = address;
            }
            return 10u;
        }
        case 0xDB: { // IN A,(n)
            const uint8_t port = fetch8();
            const uint8_t value = readIo(port);
            setRegA(value);
            return 11u;
        }
        case 0xD3: { // OUT (n),A
            const uint8_t port = fetch8();
            const uint8_t value = regA();
            writeIo(port, value);
            return 11u;
        }
        case 0xE6: { // AND n
            const uint8_t value = fetch8();
            const uint8_t result = static_cast<uint8_t>(regA() & value);
            setRegA(result);
            // Preserve unrelated flag bits, then set Z, H, S, P/V and
            // explicitly clear N and C per Z80 semantics for AND.
            uint8_t preserved = static_cast<uint8_t>(regF() & static_cast<uint8_t>(~(kFlagS | kFlagZ | kFlagH | kFlagPV | kFlagN | kFlagC)));
            uint8_t newFlags = 0u;
            if (result == 0u) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagZ);
            }
            // Half-carry is always set for AND
            newFlags = static_cast<uint8_t>(newFlags | kFlagH);
            // Sign: set if bit 7 of result is 1
            if ((result & 0x80u) != 0u) {
                newFlags = static_cast<uint8_t>(newFlags | kFlagS);
            }
            // Parity (even parity) -> P/V
            {
                int ones = 0;
                uint8_t tmp = result;
                while (tmp) { ones += tmp & 1u; tmp >>= 1; }
                if ((ones & 1) == 0) {
                    newFlags = static_cast<uint8_t>(newFlags | kFlagPV);
                }
            }
            // N and C should be cleared (newFlags does not set them)
            setRegF(static_cast<uint8_t>(preserved | newFlags));
            return 7u;
        }
        case 0xF3: { // DI
            // Disable interrupts immediately and cancel any pending EI.
            IME = false;
            imeEnableDelay_ = 0;
            return 4u;
        }
        case 0xFB: { // EI (deferred enable)
            // On Z80, EI enables interrupts only after the instruction
            // following EI has executed. Use a small delay counter here;
            // `step()` will decrement the delay and promote IME when it
            // reaches zero.
            imeEnableDelay_ = 2;
            return 4u;
        }
            case 0xED: {
                // ED-prefixed opcodes: implement a small subset (RETI, RETN later).
                const uint8_t sub = fetch8();
                switch (sub) {
                    case 0x4Du: { // RETI
                        // Pop return address from stack (low then high)
                        const uint8_t pcl = memRead(SP);
                        const uint8_t pch = memRead(static_cast<uint16_t>(SP + 1u));
                        SP = static_cast<uint16_t>(SP + 2u);
                        PC = static_cast<uint16_t>(static_cast<uint16_t>(pcl) | (static_cast<uint16_t>(pch) << 8u));
                        // Restore IFF1 from IFF2 (Z80 semantics: RETN/RETI restore IFFs)
                        IFF1 = IFF2;
                        IME = IFF1;
                        return 14u;
                    }
                    case 0x45u: { // RETN (treat like RETI)
                        const uint8_t pcl = memRead(SP);
                        const uint8_t pch = memRead(static_cast<uint16_t>(SP + 1u));
                        SP = static_cast<uint16_t>(SP + 2u);
                        PC = static_cast<uint16_t>(static_cast<uint16_t>(pcl) | (static_cast<uint16_t>(pch) << 8u));
                        IFF1 = IFF2;
                        IME = IFF1;
                        return 14u;
                    }
                    default:
                        // Unimplemented ED sub-opcode: treat as NOP for now
                        return 8u;
                }
            }
        case 0xCB: {
            const uint8_t sub = fetch8();
            const int regIndex = sub & 0x07;
            const int opGroup = (sub >> 3) & 0x1F;
            auto getReg8 = [&](int idx) -> uint8_t {
                switch (idx) {
                    case 0: return static_cast<uint8_t>((BC >> 8) & 0x00FFu);
                    case 1: return static_cast<uint8_t>(BC & 0x00FFu);
                    case 2: return static_cast<uint8_t>((DE >> 8) & 0x00FFu);
                    case 3: return static_cast<uint8_t>(DE & 0x00FFu);
                    case 4: return static_cast<uint8_t>((HL >> 8) & 0x00FFu);
                    case 5: return static_cast<uint8_t>(HL & 0x00FFu);
                    case 6: return memRead(HL);
                    case 7: return regA();
                }
                return 0u;
            };
            auto setReg8 = [&](int idx, uint8_t v) {
                switch (idx) {
                    case 0: BC = static_cast<uint16_t>((static_cast<uint16_t>(v) << 8) | (BC & 0x00FFu)); break;
                    case 1: BC = static_cast<uint16_t>((BC & 0xFF00u) | v); break;
                    case 2: DE = static_cast<uint16_t>((static_cast<uint16_t>(v) << 8) | (DE & 0x00FFu)); break;
                    case 3: DE = static_cast<uint16_t>((DE & 0xFF00u) | v); break;
                    case 4: HL = static_cast<uint16_t>((static_cast<uint16_t>(v) << 8) | (HL & 0x00FFu)); break;
                    case 5: HL = static_cast<uint16_t>((HL & 0xFF00u) | v); break;
                    case 6: memWrite(HL, v); break;
                    case 7: setRegA(v); break;
                }
            };
            auto parityEven = [&](uint8_t x) {
                int ones = 0;
                while (x) { ones += x & 1u; x >>= 1; }
                return (ones & 1) == 0;
            };

            // Rotate/shift groups (opGroup 0..7)
            if (opGroup <= 7) {
                uint8_t val = getReg8(regIndex);
                uint8_t res = 0u;
                uint8_t newFlags = 0u;
                uint8_t carryOut = 0u;
                switch (opGroup) {
                    case 0: { // RLC
                        carryOut = (val & 0x80u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val << 1) | carryOut);
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 1: { // RRC
                        carryOut = (val & 0x01u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val >> 1) | (carryOut << 7));
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 2: { // RL
                        uint8_t carryIn = (regF() & kFlagC) ? 1u : 0u;
                        carryOut = (val & 0x80u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val << 1) | carryIn);
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 3: { // RR
                        uint8_t carryIn = (regF() & kFlagC) ? 1u : 0u;
                        carryOut = (val & 0x01u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val >> 1) | (carryIn << 7));
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 4: { // SLA
                        carryOut = (val & 0x80u) ? 1u : 0u;
                        res = static_cast<uint8_t>(val << 1);
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 5: { // SRA
                        carryOut = (val & 0x01u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val >> 1) | (val & 0x80u));
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 6: { // SLL (undocumented) - treat like SLA but set low bit
                        carryOut = (val & 0x80u) ? 1u : 0u;
                        res = static_cast<uint8_t>((val << 1) | 0x01u);
                        if (res == 0u) newFlags |= kFlagZ;
                        if ((res & 0x80u) != 0u) newFlags |= kFlagS;
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                    case 7: { // SRL
                        carryOut = (val & 0x01u) ? 1u : 0u;
                        res = static_cast<uint8_t>(val >> 1);
                        if (res == 0u) newFlags |= kFlagZ;
                        // SRL clears sign
                        if (parityEven(res)) newFlags |= kFlagPV;
                        if (carryOut) newFlags |= kFlagC;
                        break;
                    }
                }
                // Clear H and N for rotate/shift results per Z80 spec
                // (we already constructed newFlags without H and N)
                setReg8(regIndex, res);
                setRegF(newFlags);
                return (regIndex == 6) ? 15u : 8u;
            }

            // BIT/RES/SET groups
            if (opGroup >= 8 && opGroup <= 15) {
                const int bit = opGroup - 8;
                const uint8_t val = getReg8(regIndex);
                const bool bitSet = (val & (1u << bit)) != 0u;
                uint8_t preservedC = static_cast<uint8_t>(regF() & kFlagC);
                uint8_t newFlags = preservedC;
                if (!bitSet) newFlags = static_cast<uint8_t>(newFlags | kFlagZ | kFlagPV);
                if ((bit == 7) && bitSet) newFlags = static_cast<uint8_t>(newFlags | kFlagS);
                // BIT sets H and resets N
                newFlags = static_cast<uint8_t>(newFlags | kFlagH);
                setRegF(newFlags);
                return (regIndex == 6) ? 12u : 8u;
            }

            if (opGroup >= 16 && opGroup <= 23) {
                // RES b,r : clear bit
                const int bit = opGroup - 16;
                uint8_t val = getReg8(regIndex);
                val = static_cast<uint8_t>(val & static_cast<uint8_t>(~(1u << bit)));
                setReg8(regIndex, val);
                return (regIndex == 6) ? 15u : 8u;
            }

            if (opGroup >= 24 && opGroup <= 31) {
                // SET b,r : set bit
                const int bit = opGroup - 24;
                uint8_t val = getReg8(regIndex);
                val = static_cast<uint8_t>(val | static_cast<uint8_t>(1u << bit));
                setReg8(regIndex, val);
                return (regIndex == 6) ? 15u : 8u;
            }

            return 8u;
        }
        default:
            return 4u;
    }
}

uint32_t Z80Interpreter::handleInterrupts() {
    if (!interruptRequestProvider) {
        return 0u;
    }
    if (!IME) {
        return 0u;
    }

    const auto maybeData = interruptRequestProvider();
    if (!maybeData.has_value()) {
        return 0u;
    }

    // An interrupt has been requested. Behavior depends on the current
    // interrupt mode (IM 0/1/2). For IM1, perform RST 0x38 (legacy
    // behavior). For IM2, construct a vector from I and the data byte
    // and fetch the handler address from memory. IM0 falls back to IM1
    // here (most hardware uses IM1-like vectors in practice).
    if (interruptMode_ == 1u) {
        // Push PC (high then low) onto the stack
        const uint8_t pcl = static_cast<uint8_t>(PC & 0x00FFu);
        const uint8_t pch = static_cast<uint8_t>((PC >> 8) & 0x00FFu);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, pch);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, pcl);
        // Jump to 0x0038 (RST 0x38 service)
        PC = 0x0038u;
        IFF1 = false;
        // Preserve IFF2 per Z80 semantics
        IME = false;
        return 11u;
    }

    if (interruptMode_ == 2u) {
        const uint8_t data = *maybeData;
        const uint16_t vector = static_cast<uint16_t>((static_cast<uint16_t>(I) << 8u) | static_cast<uint16_t>(data));
        // Read 16-bit address from vector table (low then high)
        const uint8_t pcl = memRead(vector);
        const uint8_t pch = memRead(static_cast<uint16_t>(vector + 1u));
        // Push PC, jump to fetched address
        const uint8_t oldPcl = static_cast<uint8_t>(PC & 0x00FFu);
        const uint8_t oldPch = static_cast<uint8_t>((PC >> 8) & 0x00FFu);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, oldPch);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, oldPcl);
        PC = static_cast<uint16_t>(static_cast<uint16_t>(pcl) | (static_cast<uint16_t>(pch) << 8u));
        IFF1 = false;
        IME = false;
        return 19u;
    }

    // IM0: fall back to IM1 behavior (RST 0x38)
    {
        const uint8_t pcl = static_cast<uint8_t>(PC & 0x00FFu);
        const uint8_t pch = static_cast<uint8_t>((PC >> 8) & 0x00FFu);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, pch);
        SP = static_cast<uint16_t>(SP - 1u);
        memWrite(SP, pcl);
        PC = 0x0038u;
        IFF1 = false;
        IME = false;
        return 11u;
    }
}

uint32_t Z80Interpreter::step() {
    requireMemoryInterface();
    const uint32_t intrCycles = handleInterrupts();
    if (intrCycles != 0u) {
        return intrCycles;
    }
    const uint8_t opcode = fetch8();
    const uint32_t retired = executeOpcode(opcode);
    // Decrement any pending EI delay and promote IME when it elapses.
    if (imeEnableDelay_ > 0) {
        imeEnableDelay_ -= 1;
        if (imeEnableDelay_ == 0) {
            IME = true;
        }
    }
    return retired;
}
