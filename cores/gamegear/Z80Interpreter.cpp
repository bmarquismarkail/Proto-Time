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

uint32_t Z80Interpreter::executeOpcode(uint8_t opcode) {
    switch (opcode) {
        case 0x00: // NOP
            return 4u;
        case 0x21: // LD HL,nn
            HL = fetch16();
            return 10u;
        case 0x23: // INC HL
            HL = static_cast<uint16_t>(HL + 1u);
            return 6u;
        case 0x36: { // LD (HL),n
            const uint8_t value = fetch8();
            memWrite(HL, value);
            return 10u;
        }
        case 0x3E: { // LD A,n
            const uint8_t value = fetch8();
            AF = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | (AF & 0x00FFu));
            return 7u;
        }
        case 0x77: { // LD (HL),A
            const uint8_t value = static_cast<uint8_t>((AF >> 8) & 0x00FFu);
            memWrite(HL, value);
            return 7u;
        }
        case 0xC3: // JP nn
            PC = fetch16();
            return 10u;
        case 0xDB: { // IN A,(n)
            const uint8_t port = fetch8();
            const uint8_t value = readIo(port);
            AF = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | (AF & 0x00FFu));
            return 11u;
        }
        case 0xD3: { // OUT (n),A
            const uint8_t port = fetch8();
            const uint8_t value = static_cast<uint8_t>((AF >> 8) & 0x00FFu);
            writeIo(port, value);
            return 11u;
        }
        default:
            return 4u;
    }
}

void Z80Interpreter::handleInterrupts() {
    // TODO: Implement interrupt logic
}

uint32_t Z80Interpreter::step() {
    requireMemoryInterface();
    handleInterrupts();
    const uint8_t opcode = fetch8();
    return executeOpcode(opcode);
}
