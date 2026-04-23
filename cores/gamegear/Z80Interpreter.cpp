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
    imeEnablePending_ = false;
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
        case 0x77: { // LD (HL),A
            const uint8_t value = regA();
            memWrite(HL, value);
            return 7u;
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
            imeEnablePending_ = false;
            return 4u;
        }
        case 0xFB: { // EI (deferred enable)
            // On Z80, EI enables interrupts only after the instruction
            // following EI has executed. Set a pending flag here; step()
            // will promote it to IME after the next instruction.
            imeEnablePending_ = true;
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
                    default:
                        // Unimplemented ED sub-opcode: treat as NOP for now
                        return 8u;
                }
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
    // If EI was executed previously, it becomes effective only after the
    // following instruction completes. Promote the pending flag now.
    if (imeEnablePending_) {
        IME = true;
        imeEnablePending_ = false;
    }
    return retired;
}
