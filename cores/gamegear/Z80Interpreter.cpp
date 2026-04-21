#include "Z80Interpreter.hpp"
#include <stdexcept>

Z80Interpreter::Z80Interpreter() {}

Z80Interpreter::~Z80Interpreter() {}

void Z80Interpreter::setMemoryInterface(MemRead reader, MemWrite writer) {
    memRead = std::move(reader);
    memWrite = std::move(writer);
}

void Z80Interpreter::requireMemoryInterface() const {
    if (!memRead || !memWrite) {
        throw std::runtime_error("Z80 memory interface not set");
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

void Z80Interpreter::executeOpcode(uint8_t opcode) {
    // TODO: Implement full Z80 opcode table
    // For now, NOP (0x00) only
    switch (opcode) {
        case 0x00: /* NOP */ break;
        default: /* Unimplemented */ break;
    }
}

void Z80Interpreter::handleInterrupts() {
    // TODO: Implement interrupt logic
}

uint32_t Z80Interpreter::step() {
    requireMemoryInterface();
    handleInterrupts();
    const uint8_t opcode = fetch8();
    executeOpcode(opcode);
    return 4u;
}
