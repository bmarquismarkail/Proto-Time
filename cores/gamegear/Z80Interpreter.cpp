#include "Z80Interpreter.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace {
constexpr uint8_t fS = 0x80u;
constexpr uint8_t fZ = 0x40u;
constexpr uint8_t f5 = 0x20u;
constexpr uint8_t f3 = 0x08u;

uint8_t hi(uint16_t value) noexcept { return static_cast<uint8_t>(value >> 8u); }
uint8_t lo(uint16_t value) noexcept { return static_cast<uint8_t>(value & 0x00FFu); }
uint16_t word(uint8_t low, uint8_t high) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8u));
}
uint8_t sz35(uint8_t value) noexcept {
    uint8_t flags = static_cast<uint8_t>(value & (fS | f5 | f3));
    if (value == 0u) {
        flags = static_cast<uint8_t>(flags | fZ);
    }
    return flags;
}
int8_t displacement(uint8_t value) noexcept {
    return static_cast<int8_t>(value);
}
}

uint8_t Z80Interpreter::computeIncFlags(uint8_t before, uint8_t result) const noexcept {
    uint8_t flags = static_cast<uint8_t>(regF() & kFlagC);
    flags = static_cast<uint8_t>(flags | (result & (kFlagS | kFlag5 | kFlag3)));
    if (result == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
    if (((before & 0x0Fu) + 1u) > 0x0Fu) flags = static_cast<uint8_t>(flags | kFlagH);
    if (before == 0x7Fu) flags = static_cast<uint8_t>(flags | kFlagPV);
    return flags;
}

uint8_t Z80Interpreter::computeDecFlags(uint8_t before, uint8_t result) const noexcept {
    uint8_t flags = static_cast<uint8_t>((regF() & kFlagC) | kFlagN);
    flags = static_cast<uint8_t>(flags | (result & (kFlagS | kFlag5 | kFlag3)));
    if (result == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
    if ((before & 0x0Fu) == 0u) flags = static_cast<uint8_t>(flags | kFlagH);
    if (before == 0x80u) flags = static_cast<uint8_t>(flags | kFlagPV);
    return flags;
}

Z80Interpreter::Z80Interpreter() = default;
Z80Interpreter::~Z80Interpreter() = default;

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
    AF = 0x01B0u;
    BC = DE = HL = IX = IY = 0u;
    SP = 0xDFF0u;
    PC = 0u;
    AF_ = BC_ = DE_ = HL_ = 0u;
    I = R = 0u;
    IFF1 = IFF2 = IME = false;
    imeEnableDelay_ = 0;
    halted_ = false;
    interruptMode_ = 1u;
}

void Z80Interpreter::incRefresh() noexcept {
    R = static_cast<uint8_t>((R & 0x80u) | ((R + 1u) & 0x7Fu));
}

uint8_t Z80Interpreter::fetch8() {
    requireMemoryInterface();
    const uint8_t val = memRead(PC);
    PC = static_cast<uint16_t>(PC + 1u);
    return val;
}

uint8_t Z80Interpreter::fetchOpcode() {
    incRefresh();
    return fetch8();
}

uint16_t Z80Interpreter::fetch16() {
    const uint8_t low = fetch8();
    const uint8_t high = fetch8();
    return word(low, high);
}

uint8_t Z80Interpreter::regA() const noexcept { return hi(AF); }
void Z80Interpreter::setRegA(uint8_t value) noexcept { AF = word(regF(), value); }
uint8_t Z80Interpreter::regF() const noexcept { return lo(AF); }
void Z80Interpreter::setRegF(uint8_t value) noexcept { AF = static_cast<uint16_t>((AF & 0xFF00u) | value); }

void Z80Interpreter::setZeroFlag(bool set) noexcept {
    uint8_t flags = static_cast<uint8_t>(regF() & ~kFlagZ);
    if (set) flags = static_cast<uint8_t>(flags | kFlagZ);
    setRegF(flags);
}

bool Z80Interpreter::zeroFlag() const noexcept { return (regF() & kFlagZ) != 0u; }

uint8_t Z80Interpreter::readReg8(int idx) const {
    switch (idx & 7) {
    case 0: return hi(BC);
    case 1: return lo(BC);
    case 2: return hi(DE);
    case 3: return lo(DE);
    case 4: return hi(HL);
    case 5: return lo(HL);
    case 6: return memRead(HL);
    default: return regA();
    }
}

void Z80Interpreter::writeReg8(int idx, uint8_t value) {
    switch (idx & 7) {
    case 0: BC = word(lo(BC), value); return;
    case 1: BC = word(value, hi(BC)); return;
    case 2: DE = word(lo(DE), value); return;
    case 3: DE = word(value, hi(DE)); return;
    case 4: HL = word(lo(HL), value); return;
    case 5: HL = word(value, hi(HL)); return;
    case 6: memWrite(HL, value); return;
    default: setRegA(value); return;
    }
}

bool Z80Interpreter::parityEven(uint8_t value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_parity(static_cast<unsigned int>(value)) == 0;
#else
    value ^= static_cast<uint8_t>(value >> 4u);
    value ^= static_cast<uint8_t>(value >> 2u);
    value ^= static_cast<uint8_t>(value >> 1u);
    return (value & 1u) == 0u;
#endif
}

uint8_t Z80Interpreter::computeAddFlags(uint8_t lhs, uint8_t rhs, uint8_t carryIn, uint8_t result) const noexcept {
    const uint16_t wide = static_cast<uint16_t>(lhs) + rhs + carryIn;
    uint8_t flags = static_cast<uint8_t>(result & (kFlagS | kFlag5 | kFlag3));
    if (result == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
    if (((lhs & 0x0Fu) + (rhs & 0x0Fu) + carryIn) > 0x0Fu) flags = static_cast<uint8_t>(flags | kFlagH);
    if (((~(lhs ^ rhs)) & (lhs ^ result) & 0x80u) != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
    if (wide > 0xFFu) flags = static_cast<uint8_t>(flags | kFlagC);
    return flags;
}

uint8_t Z80Interpreter::computeSubFlags(uint8_t lhs, uint8_t rhs, uint8_t carryIn, uint8_t result) const noexcept {
    uint8_t flags = static_cast<uint8_t>(kFlagN | (result & (kFlagS | kFlag5 | kFlag3)));
    if (result == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
    if ((lhs & 0x0Fu) < ((rhs & 0x0Fu) + carryIn)) flags = static_cast<uint8_t>(flags | kFlagH);
    if ((((lhs ^ rhs) & (lhs ^ result)) & 0x80u) != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
    if (static_cast<uint16_t>(lhs) < static_cast<uint16_t>(rhs + carryIn)) flags = static_cast<uint8_t>(flags | kFlagC);
    return flags;
}

void Z80Interpreter::setLogicFlags(uint8_t result, bool halfCarry) noexcept {
    uint8_t flags = sz35(result);
    if (halfCarry) flags = static_cast<uint8_t>(flags | kFlagH);
    if (parityEven(result)) flags = static_cast<uint8_t>(flags | kFlagPV);
    setRegF(flags);
}

void Z80Interpreter::push16(uint16_t value) {
    SP = static_cast<uint16_t>(SP - 1u);
    memWrite(SP, hi(value));
    SP = static_cast<uint16_t>(SP - 1u);
    memWrite(SP, lo(value));
}

uint16_t Z80Interpreter::pop16() {
    const uint8_t low = memRead(SP);
    const uint8_t high = memRead(static_cast<uint16_t>(SP + 1u));
    SP = static_cast<uint16_t>(SP + 2u);
    return word(low, high);
}

void Z80Interpreter::write16(uint16_t address, uint16_t value) {
    memWrite(address, lo(value));
    memWrite(static_cast<uint16_t>(address + 1u), hi(value));
}

uint16_t Z80Interpreter::read16(uint16_t address) const {
    return word(memRead(address), memRead(static_cast<uint16_t>(address + 1u)));
}

uint16_t Z80Interpreter::getReg16ByPair(int pair, bool afForPair3) const noexcept {
    switch (pair & 3) {
    case 0: return BC;
    case 1: return DE;
    case 2: return HL;
    default: return afForPair3 ? AF : SP;
    }
}

void Z80Interpreter::setReg16ByPair(int pair, uint16_t value, bool afForPair3) noexcept {
    switch (pair & 3) {
    case 0: BC = value; return;
    case 1: DE = value; return;
    case 2: HL = value; return;
    default: if (afForPair3) AF = value; else SP = value; return;
    }
}

uint8_t Z80Interpreter::alu(uint8_t group, uint8_t value) {
    const uint8_t a = regA();
    uint8_t result = 0u;
    switch (group & 7u) {
    case 0: result = static_cast<uint8_t>(a + value); setRegA(result); setRegF(computeAddFlags(a, value, 0u, result)); break;
    case 1: {
        const uint8_t c = (regF() & kFlagC) != 0u ? 1u : 0u;
        result = static_cast<uint8_t>(a + value + c); setRegA(result); setRegF(computeAddFlags(a, value, c, result)); break;
    }
    case 2: result = static_cast<uint8_t>(a - value); setRegA(result); setRegF(computeSubFlags(a, value, 0u, result)); break;
    case 3: {
        const uint8_t c = (regF() & kFlagC) != 0u ? 1u : 0u;
        result = static_cast<uint8_t>(a - value - c); setRegA(result); setRegF(computeSubFlags(a, value, c, result)); break;
    }
    case 4: result = static_cast<uint8_t>(a & value); setRegA(result); setLogicFlags(result, true); break;
    case 5: result = static_cast<uint8_t>(a ^ value); setRegA(result); setLogicFlags(result, false); break;
    case 6: result = static_cast<uint8_t>(a | value); setRegA(result); setLogicFlags(result, false); break;
    default: result = static_cast<uint8_t>(a - value); setRegF(computeSubFlags(a, value, 0u, result)); break;
    }
    return result;
}

uint8_t Z80Interpreter::rotateShift(uint8_t group, uint8_t value) {
    uint8_t result = value;
    uint8_t carry = 0u;
    switch (group & 7u) {
    case 0: carry = static_cast<uint8_t>((value >> 7u) & 1u); result = static_cast<uint8_t>((value << 1u) | carry); break; // RLC
    case 1: carry = static_cast<uint8_t>(value & 1u); result = static_cast<uint8_t>((value >> 1u) | (carry << 7u)); break; // RRC
    case 2: carry = static_cast<uint8_t>((value >> 7u) & 1u); result = static_cast<uint8_t>((value << 1u) | ((regF() & kFlagC) != 0u ? 1u : 0u)); break; // RL
    case 3: carry = static_cast<uint8_t>(value & 1u); result = static_cast<uint8_t>((value >> 1u) | ((regF() & kFlagC) != 0u ? 0x80u : 0u)); break; // RR
    case 4: carry = static_cast<uint8_t>((value >> 7u) & 1u); result = static_cast<uint8_t>(value << 1u); break; // SLA
    case 5: carry = static_cast<uint8_t>(value & 1u); result = static_cast<uint8_t>((value >> 1u) | (value & 0x80u)); break; // SRA
    case 6: carry = static_cast<uint8_t>((value >> 7u) & 1u); result = static_cast<uint8_t>((value << 1u) | 1u); break; // SLL/SLI undocumented
    default: carry = static_cast<uint8_t>(value & 1u); result = static_cast<uint8_t>(value >> 1u); break; // SRL
    }
    uint8_t flags = sz35(result);
    if (parityEven(result)) flags = static_cast<uint8_t>(flags | kFlagPV);
    if (carry != 0u) flags = static_cast<uint8_t>(flags | kFlagC);
    setRegF(flags);
    return result;
}

uint32_t Z80Interpreter::executeCbOpcode(uint8_t opcode, bool indexed, uint16_t indexedAddress, uint8_t* indexedDestination) {
    const uint8_t x = static_cast<uint8_t>(opcode >> 6u);
    const uint8_t y = static_cast<uint8_t>((opcode >> 3u) & 7u);
    const uint8_t z = static_cast<uint8_t>(opcode & 7u);
    const bool memTarget = indexed || z == 6u;
    uint8_t value = indexed ? memRead(indexedAddress) : readReg8(z);

    if (x == 0u) {
        const uint8_t result = rotateShift(y, value);
        if (indexed) {
            memWrite(indexedAddress, result);
            if (indexedDestination != nullptr) *indexedDestination = result;
        } else {
            writeReg8(z, result);
        }
        return indexed ? 23u : (memTarget ? 15u : 8u);
    }
    if (x == 1u) {
        const bool zero = (value & static_cast<uint8_t>(1u << y)) == 0u;
        uint8_t flags = static_cast<uint8_t>((regF() & kFlagC) | kFlagH);
        if (zero) flags = static_cast<uint8_t>(flags | kFlagZ | kFlagPV);
        if (y == 7u && !zero) flags = static_cast<uint8_t>(flags | kFlagS);
        const uint8_t sourceFor35 = indexed ? hi(indexedAddress) : value;
        flags = static_cast<uint8_t>(flags | (sourceFor35 & (kFlag5 | kFlag3)));
        setRegF(flags);
        return indexed ? 20u : (memTarget ? 12u : 8u);
    }
    if (x == 2u) {
        value = static_cast<uint8_t>(value & ~static_cast<uint8_t>(1u << y));
    } else {
        value = static_cast<uint8_t>(value | static_cast<uint8_t>(1u << y));
    }
    if (indexed) {
        memWrite(indexedAddress, value);
        if (indexedDestination != nullptr) *indexedDestination = value;
    } else {
        writeReg8(z, value);
    }
    return indexed ? 23u : (memTarget ? 15u : 8u);
}

uint32_t Z80Interpreter::executeEdOpcode(uint8_t opcode) {
    const uint8_t x = static_cast<uint8_t>(opcode >> 6u);
    const uint8_t y = static_cast<uint8_t>((opcode >> 3u) & 7u);
    const uint8_t z = static_cast<uint8_t>(opcode & 7u);
    const uint8_t p = static_cast<uint8_t>((y >> 1u) & 3u);
    const uint8_t q = static_cast<uint8_t>(y & 1u);

    if (x == 1u) {
        if (z == 0u) { // IN r,(C), with y==6 discarding the value
            const uint8_t value = readIo(lo(BC));
            uint8_t flags = static_cast<uint8_t>((regF() & kFlagC) | sz35(value));
            if (parityEven(value)) flags = static_cast<uint8_t>(flags | kFlagPV);
            setRegF(flags);
            if (y != 6u) writeReg8(y, value);
            return 12u;
        }
        if (z == 1u) { // OUT (C),r, with y==6 writing zero
            writeIo(lo(BC), y == 6u ? 0u : readReg8(y));
            return 12u;
        }
        if (z == 2u) { // SBC/ADC HL,ss
            const uint16_t rhs = getReg16ByPair(p);
            const uint32_t lhs = HL;
            const uint32_t carry = (regF() & kFlagC) != 0u ? 1u : 0u;
            uint32_t result = 0u;
            uint8_t flags = 0u;
            if (q == 0u) {
                result = lhs - rhs - carry;
                const uint16_t r16 = static_cast<uint16_t>(result);
                flags = static_cast<uint8_t>(kFlagN | (hi(r16) & (kFlagS | kFlag5 | kFlag3)));
                if (r16 == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
                if ((lhs & 0x0FFFu) < ((rhs & 0x0FFFu) + carry)) flags = static_cast<uint8_t>(flags | kFlagH);
                if (((lhs ^ rhs) & (lhs ^ r16) & 0x8000u) != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
                if (lhs < rhs + carry) flags = static_cast<uint8_t>(flags | kFlagC);
                HL = r16;
            } else {
                result = lhs + rhs + carry;
                const uint16_t r16 = static_cast<uint16_t>(result);
                flags = static_cast<uint8_t>(hi(r16) & (kFlagS | kFlag5 | kFlag3));
                if (r16 == 0u) flags = static_cast<uint8_t>(flags | kFlagZ);
                if (((lhs & 0x0FFFu) + (rhs & 0x0FFFu) + carry) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
                if (((~(lhs ^ rhs)) & (lhs ^ r16) & 0x8000u) != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
                if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
                HL = r16;
            }
            setRegF(flags);
            return 15u;
        }
        if (z == 3u) { // LD (nn),rr / LD rr,(nn)
            const uint16_t address = fetch16();
            if (q == 0u) {
                write16(address, getReg16ByPair(p));
            } else {
                setReg16ByPair(p, read16(address));
            }
            return 20u;
        }
        if (z == 4u) { // NEG and undocumented duplicates
            const uint8_t a = regA();
            const uint8_t result = static_cast<uint8_t>(0u - a);
            setRegA(result);
            setRegF(computeSubFlags(0u, a, 0u, result));
            return 8u;
        }
        if (z == 5u) { // RETN/RETI duplicates
            PC = pop16();
            IFF1 = IFF2;
            IME = IFF1;
            return 14u;
        }
        if (z == 6u) {
            if (y == 0u || y == 4u) interruptMode_ = 0u;
            else if (y == 2u || y == 6u) interruptMode_ = 1u;
            else interruptMode_ = 2u;
            return 8u;
        }
        if (z == 7u) {
            switch (y) {
            case 0: I = regA(); return 9u; // LD I,A
            case 1: R = regA(); return 9u; // LD R,A
            case 2: { // LD A,I
                const uint8_t value = I;
                setRegA(value);
                uint8_t flags = static_cast<uint8_t>((regF() & kFlagC) | sz35(value));
                if (IFF2) flags = static_cast<uint8_t>(flags | kFlagPV);
                setRegF(flags);
                return 9u;
            }
            case 3: { // LD A,R
                const uint8_t value = R;
                setRegA(value);
                uint8_t flags = static_cast<uint8_t>((regF() & kFlagC) | sz35(value));
                if (IFF2) flags = static_cast<uint8_t>(flags | kFlagPV);
                setRegF(flags);
                return 9u;
            }
            case 4: { // RRD
                const uint8_t mem = memRead(HL);
                const uint8_t a = regA();
                const uint8_t carry = static_cast<uint8_t>(regF() & kFlagC);
                memWrite(HL, static_cast<uint8_t>((a << 4u) | (mem >> 4u)));
                setRegA(static_cast<uint8_t>((a & 0xF0u) | (mem & 0x0Fu)));
                setLogicFlags(regA(), false);
                setRegF(static_cast<uint8_t>(regF() | carry));
                return 18u;
            }
            case 5: { // RLD
                const uint8_t mem = memRead(HL);
                const uint8_t a = regA();
                const uint8_t carry = static_cast<uint8_t>(regF() & kFlagC);
                memWrite(HL, static_cast<uint8_t>((mem << 4u) | (a & 0x0Fu)));
                setRegA(static_cast<uint8_t>((a & 0xF0u) | (mem >> 4u)));
                setLogicFlags(regA(), false);
                setRegF(static_cast<uint8_t>(regF() | carry));
                return 18u;
            }
            default:
                return 8u; // ED NOPs
            }
        }
    }

    auto blockTransfer = [&](int direction, bool repeat) -> uint32_t {
        uint32_t cycles = 0u;
        do {
            memWrite(DE, memRead(HL));
            HL = static_cast<uint16_t>(HL + direction);
            DE = static_cast<uint16_t>(DE + direction);
            BC = static_cast<uint16_t>(BC - 1u);
            uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagC));
            if (BC != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
            setRegF(flags);
            cycles += (repeat && BC != 0u) ? 21u : 16u;
        } while (repeat && BC != 0u);
        return cycles;
    };
    auto blockCompare = [&](int direction, bool repeat) -> uint32_t {
        uint32_t cycles = 0u;
        do {
            const uint8_t value = memRead(HL);
            const uint8_t a = regA();
            const uint8_t result = static_cast<uint8_t>(a - value);
            HL = static_cast<uint16_t>(HL + direction);
            BC = static_cast<uint16_t>(BC - 1u);
            uint8_t flags = computeSubFlags(a, value, 0u, result);
            flags = static_cast<uint8_t>(flags & ~kFlagC);
            flags = static_cast<uint8_t>(flags | (regF() & kFlagC));
            if (BC != 0u && result != 0u) flags = static_cast<uint8_t>(flags | kFlagPV);
            else flags = static_cast<uint8_t>(flags & ~kFlagPV);
            setRegF(flags);
            cycles += (repeat && BC != 0u && result != 0u) ? 21u : 16u;
            if (result == 0u) break;
        } while (repeat && BC != 0u);
        return cycles;
    };
    auto blockIn = [&](int direction, bool repeat) -> uint32_t {
        uint32_t cycles = 0u;
        do {
            memWrite(HL, readIo(lo(BC)));
            HL = static_cast<uint16_t>(HL + direction);
            const uint8_t b = static_cast<uint8_t>(hi(BC) - 1u);
            BC = word(lo(BC), b);
            uint8_t flags = static_cast<uint8_t>(sz35(b) | kFlagN);
            setRegF(flags);
            cycles += (repeat && b != 0u) ? 21u : 16u;
        } while (repeat && hi(BC) != 0u);
        return cycles;
    };
    auto blockOut = [&](int direction, bool repeat) -> uint32_t {
        uint32_t cycles = 0u;
        do {
            const uint8_t value = memRead(HL);
            HL = static_cast<uint16_t>(HL + direction);
            const uint8_t b = static_cast<uint8_t>(hi(BC) - 1u);
            BC = word(lo(BC), b);
            writeIo(lo(BC), value);
            uint8_t flags = static_cast<uint8_t>(sz35(b) | kFlagN);
            setRegF(flags);
            cycles += (repeat && b != 0u) ? 21u : 16u;
        } while (repeat && hi(BC) != 0u);
        return cycles;
    };

    switch (opcode) {
    case 0xA0: return blockTransfer(+1, false); // LDI
    case 0xA1: return blockCompare(+1, false);  // CPI
    case 0xA2: return blockIn(+1, false);       // INI
    case 0xA3: return blockOut(+1, false);      // OUTI
    case 0xA8: return blockTransfer(-1, false); // LDD
    case 0xA9: return blockCompare(-1, false);  // CPD
    case 0xAA: return blockIn(-1, false);       // IND
    case 0xAB: return blockOut(-1, false);      // OUTD
    case 0xB0: return blockTransfer(+1, true);  // LDIR
    case 0xB1: return blockCompare(+1, true);   // CPIR
    case 0xB2: return blockIn(+1, true);        // INIR
    case 0xB3: return blockOut(+1, true);       // OTIR
    case 0xB8: return blockTransfer(-1, true);  // LDDR
    case 0xB9: return blockCompare(-1, true);   // CPDR
    case 0xBA: return blockIn(-1, true);        // INDR
    case 0xBB: return blockOut(-1, true);       // OTDR
    default: return 8u; // ED NOP space
    }
}

uint32_t Z80Interpreter::executeIndexedOpcode(uint8_t prefix, uint8_t opcode) {
    uint16_t& index = (prefix == 0xDDu) ? IX : IY;
    auto readIndexedReg = [&](uint8_t r) -> uint8_t {
        if (r == 4u) return hi(index);
        if (r == 5u) return lo(index);
        return readReg8(r);
    };
    auto writeIndexedReg = [&](uint8_t r, uint8_t value) {
        if (r == 4u) index = word(lo(index), value);
        else if (r == 5u) index = word(value, hi(index));
        else writeReg8(r, value);
    };
    auto indexedAddress = [&]() -> uint16_t {
        return static_cast<uint16_t>(index + displacement(fetch8()));
    };

    if (opcode == 0xCBu) {
        const uint16_t address = indexedAddress();
        const uint8_t cb = fetchOpcode();
        uint8_t destination = 0u;
        uint8_t* destPtr = ((cb & 0xC0u) != 0x40u && (cb & 7u) != 6u) ? &destination : nullptr;
        const uint32_t cycles = executeCbOpcode(cb, true, address, destPtr);
        if (destPtr != nullptr) writeIndexedReg(cb & 7u, destination);
        return cycles;
    }

    switch (opcode) {
    case 0x09: {
        const uint32_t result = static_cast<uint32_t>(index) + BC;
        uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagPV));
        if (((index & 0x0FFFu) + (BC & 0x0FFFu)) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
        if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
        index = static_cast<uint16_t>(result);
        flags = static_cast<uint8_t>(flags | (hi(index) & (kFlag5 | kFlag3)));
        setRegF(flags);
        return 15u;
    }
    case 0x19: {
        const uint32_t result = static_cast<uint32_t>(index) + DE;
        uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagPV));
        if (((index & 0x0FFFu) + (DE & 0x0FFFu)) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
        if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
        index = static_cast<uint16_t>(result);
        flags = static_cast<uint8_t>(flags | (hi(index) & (kFlag5 | kFlag3)));
        setRegF(flags);
        return 15u;
    }
    case 0x21: index = fetch16(); return 14u;
    case 0x22: write16(fetch16(), index); return 20u;
    case 0x23: index = static_cast<uint16_t>(index + 1u); return 10u;
    case 0x24: { const uint8_t b = hi(index); const uint8_t r = static_cast<uint8_t>(b + 1u); index = word(lo(index), r); setRegF(computeIncFlags(b, r)); return 8u; }
    case 0x25: { const uint8_t b = hi(index); const uint8_t r = static_cast<uint8_t>(b - 1u); index = word(lo(index), r); setRegF(computeDecFlags(b, r)); return 8u; }
    case 0x26: index = word(lo(index), fetch8()); return 11u;
    case 0x29: {
        const uint32_t result = static_cast<uint32_t>(index) + index;
        uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagPV));
        if (((index & 0x0FFFu) + (index & 0x0FFFu)) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
        if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
        index = static_cast<uint16_t>(result);
        flags = static_cast<uint8_t>(flags | (hi(index) & (kFlag5 | kFlag3)));
        setRegF(flags);
        return 15u;
    }
    case 0x2A: index = read16(fetch16()); return 20u;
    case 0x2B: index = static_cast<uint16_t>(index - 1u); return 10u;
    case 0x2C: { const uint8_t b = lo(index); const uint8_t r = static_cast<uint8_t>(b + 1u); index = word(r, hi(index)); setRegF(computeIncFlags(b, r)); return 8u; }
    case 0x2D: { const uint8_t b = lo(index); const uint8_t r = static_cast<uint8_t>(b - 1u); index = word(r, hi(index)); setRegF(computeDecFlags(b, r)); return 8u; }
    case 0x2E: index = word(fetch8(), hi(index)); return 11u;
    case 0x34: { const uint16_t a = indexedAddress(); const uint8_t b = memRead(a); const uint8_t r = static_cast<uint8_t>(b + 1u); memWrite(a, r); setRegF(computeIncFlags(b, r)); return 23u; }
    case 0x35: { const uint16_t a = indexedAddress(); const uint8_t b = memRead(a); const uint8_t r = static_cast<uint8_t>(b - 1u); memWrite(a, r); setRegF(computeDecFlags(b, r)); return 23u; }
    case 0x36: { const uint16_t a = indexedAddress(); memWrite(a, fetch8()); return 19u; }
    case 0x39: {
        const uint32_t result = static_cast<uint32_t>(index) + SP;
        uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagPV));
        if (((index & 0x0FFFu) + (SP & 0x0FFFu)) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
        if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
        index = static_cast<uint16_t>(result);
        flags = static_cast<uint8_t>(flags | (hi(index) & (kFlag5 | kFlag3)));
        setRegF(flags);
        return 15u;
    }
    case 0xE1: index = pop16(); return 14u;
    case 0xE3: { const uint16_t temp = read16(SP); write16(SP, index); index = temp; return 23u; }
    case 0xE5: push16(index); return 15u;
    case 0xE9: PC = index; return 8u;
    case 0xF9: SP = index; return 10u;
    default:
        break;
    }

    if ((opcode & 0xC7u) == 0x06u && opcode != 0x36u) {
        writeIndexedReg((opcode >> 3u) & 7u, fetch8());
        return 11u;
    }
    if ((opcode & 0xC0u) == 0x40u && opcode != 0x76u) {
        const uint8_t dst = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const uint8_t src = static_cast<uint8_t>(opcode & 7u);
        if (src == 6u) {
            const uint16_t address = indexedAddress();
            writeIndexedReg(dst, memRead(address));
            return 19u;
        }
        if (dst == 6u) {
            const uint16_t address = indexedAddress();
            memWrite(address, readIndexedReg(src));
            return 19u;
        }
        writeIndexedReg(dst, readIndexedReg(src));
        return 8u;
    }
    if ((opcode & 0xC0u) == 0x80u) {
        const uint8_t src = static_cast<uint8_t>(opcode & 7u);
        const uint8_t value = src == 6u ? memRead(indexedAddress()) : readIndexedReg(src);
        (void)alu(static_cast<uint8_t>((opcode >> 3u) & 7u), value);
        return src == 6u ? 19u : 8u;
    }

    return executeOpcode(opcode);
}

uint32_t Z80Interpreter::executeOpcode(uint8_t opcode) {
    if ((opcode & 0xC0u) == 0x40u) {
        if (opcode == 0x76u) {
            halted_ = true;
            return 4u;
        }
        const uint8_t dst = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const uint8_t src = static_cast<uint8_t>(opcode & 7u);
        writeReg8(dst, readReg8(src));
        return (dst == 6u || src == 6u) ? 7u : 4u;
    }
    if ((opcode & 0xC0u) == 0x80u) {
        const uint8_t src = static_cast<uint8_t>(opcode & 7u);
        (void)alu(static_cast<uint8_t>((opcode >> 3u) & 7u), readReg8(src));
        return src == 6u ? 7u : 4u;
    }
    if ((opcode & 0xC7u) == 0x06u) {
        const uint8_t dst = static_cast<uint8_t>((opcode >> 3u) & 7u);
        writeReg8(dst, fetch8());
        return dst == 6u ? 10u : 7u;
    }
    if ((opcode & 0xC7u) == 0x04u) {
        const uint8_t dst = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const uint8_t before = readReg8(dst);
        const uint8_t result = static_cast<uint8_t>(before + 1u);
        writeReg8(dst, result);
        setRegF(computeIncFlags(before, result));
        return dst == 6u ? 11u : 4u;
    }
    if ((opcode & 0xC7u) == 0x05u) {
        const uint8_t dst = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const uint8_t before = readReg8(dst);
        const uint8_t result = static_cast<uint8_t>(before - 1u);
        writeReg8(dst, result);
        setRegF(computeDecFlags(before, result));
        return dst == 6u ? 11u : 4u;
    }

    switch (opcode) {
    case 0x00: return 4u;
    case 0x01: case 0x11: case 0x21: case 0x31:
        setReg16ByPair((opcode >> 4u) & 3u, fetch16());
        return 10u;
    case 0x02: memWrite(BC, regA()); return 7u;
    case 0x03: case 0x13: case 0x23: case 0x33:
        setReg16ByPair((opcode >> 4u) & 3u, static_cast<uint16_t>(getReg16ByPair((opcode >> 4u) & 3u) + 1u));
        return 6u;
    case 0x07: {
        const uint8_t a = regA();
        const uint8_t c = static_cast<uint8_t>(a >> 7u);
        const uint8_t r = static_cast<uint8_t>((a << 1u) | c);
        setRegA(r);
        setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | (r & (kFlag5 | kFlag3)) | c));
        return 4u;
    }
    case 0x08: std::swap(AF, AF_); return 4u;
    case 0x09: case 0x19: case 0x29: case 0x39: {
        const uint16_t rhs = getReg16ByPair((opcode >> 4u) & 3u);
        const uint32_t result = static_cast<uint32_t>(HL) + rhs;
        uint8_t flags = static_cast<uint8_t>(regF() & (kFlagS | kFlagZ | kFlagPV));
        if (((HL & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu) flags = static_cast<uint8_t>(flags | kFlagH);
        if (result > 0xFFFFu) flags = static_cast<uint8_t>(flags | kFlagC);
        HL = static_cast<uint16_t>(result);
        flags = static_cast<uint8_t>(flags | (hi(HL) & (kFlag5 | kFlag3)));
        setRegF(flags);
        return 11u;
    }
    case 0x0A: setRegA(memRead(BC)); return 7u;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B:
        setReg16ByPair((opcode >> 4u) & 3u, static_cast<uint16_t>(getReg16ByPair((opcode >> 4u) & 3u) - 1u));
        return 6u;
    case 0x0F: {
        const uint8_t a = regA();
        const uint8_t c = static_cast<uint8_t>(a & 1u);
        const uint8_t r = static_cast<uint8_t>((a >> 1u) | (c << 7u));
        setRegA(r);
        setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | (r & (kFlag5 | kFlag3)) | c));
        return 4u;
    }
    case 0x10: {
        const int8_t d = displacement(fetch8());
        const uint8_t b = static_cast<uint8_t>(hi(BC) - 1u);
        BC = word(lo(BC), b);
        if (b != 0u) { PC = static_cast<uint16_t>(PC + d); return 13u; }
        return 8u;
    }
    case 0x12: memWrite(DE, regA()); return 7u;
    case 0x17: {
        const uint8_t a = regA();
        const uint8_t c = static_cast<uint8_t>(a >> 7u);
        const uint8_t r = static_cast<uint8_t>((a << 1u) | ((regF() & kFlagC) != 0u ? 1u : 0u));
        setRegA(r);
        setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | (r & (kFlag5 | kFlag3)) | c));
        return 4u;
    }
    case 0x18: PC = static_cast<uint16_t>(PC + displacement(fetch8())); return 12u;
    case 0x1A: setRegA(memRead(DE)); return 7u;
    case 0x1F: {
        const uint8_t a = regA();
        const uint8_t c = static_cast<uint8_t>(a & 1u);
        const uint8_t r = static_cast<uint8_t>((a >> 1u) | ((regF() & kFlagC) != 0u ? 0x80u : 0u));
        setRegA(r);
        setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | (r & (kFlag5 | kFlag3)) | c));
        return 4u;
    }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        const int8_t d = displacement(fetch8());
        const bool z = zeroFlag();
        const bool c = (regF() & kFlagC) != 0u;
        const bool take = (opcode == 0x20u && !z) || (opcode == 0x28u && z) || (opcode == 0x30u && !c) || (opcode == 0x38u && c);
        if (take) { PC = static_cast<uint16_t>(PC + d); return 12u; }
        return 7u;
    }
    case 0x22: write16(fetch16(), HL); return 16u;
    case 0x27: { // DAA
        uint8_t a = regA();
        uint8_t correction = 0u;
        uint8_t carry = regF() & kFlagC;
        if ((regF() & kFlagH) != 0u || ((a & 0x0Fu) > 9u)) correction = static_cast<uint8_t>(correction | 0x06u);
        if (carry != 0u || a > 0x99u) { correction = static_cast<uint8_t>(correction | 0x60u); carry = kFlagC; }
        a = (regF() & kFlagN) != 0u ? static_cast<uint8_t>(a - correction) : static_cast<uint8_t>(a + correction);
        setRegA(a);
        uint8_t flags = static_cast<uint8_t>((regF() & kFlagN) | carry | sz35(a));
        if (parityEven(a)) flags = static_cast<uint8_t>(flags | kFlagPV);
        setRegF(flags);
        return 4u;
    }
    case 0x2A: HL = read16(fetch16()); return 16u;
    case 0x2F: setRegA(static_cast<uint8_t>(~regA())); setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV | kFlagC)) | kFlagH | kFlagN | (regA() & (kFlag5 | kFlag3)))); return 4u;
    case 0x32: memWrite(fetch16(), regA()); return 13u;
    case 0x37: setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | kFlagC | (regA() & (kFlag5 | kFlag3)))); return 4u;
    case 0x3A: setRegA(memRead(fetch16())); return 13u;
    case 0x3F: {
        const uint8_t oldCarry = static_cast<uint8_t>(regF() & kFlagC);
        setRegF(static_cast<uint8_t>((regF() & (kFlagS | kFlagZ | kFlagPV)) | (oldCarry != 0u ? kFlagH : 0u) | (oldCarry != 0u ? 0u : kFlagC) | (regA() & (kFlag5 | kFlag3))));
        return 4u;
    }
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8: {
        const bool cond = ((opcode >> 3u) & 7u) == 0u ? !zeroFlag() :
                          ((opcode >> 3u) & 7u) == 1u ? zeroFlag() :
                          ((opcode >> 3u) & 7u) == 2u ? ((regF() & kFlagC) == 0u) :
                          ((opcode >> 3u) & 7u) == 3u ? ((regF() & kFlagC) != 0u) :
                          ((opcode >> 3u) & 7u) == 4u ? ((regF() & kFlagPV) == 0u) :
                          ((opcode >> 3u) & 7u) == 5u ? ((regF() & kFlagPV) != 0u) :
                          ((opcode >> 3u) & 7u) == 6u ? ((regF() & kFlagS) == 0u) : ((regF() & kFlagS) != 0u);
        if (cond) { PC = pop16(); return 11u; }
        return 5u;
    }
    case 0xC1: case 0xD1: case 0xE1: case 0xF1:
        setReg16ByPair((opcode >> 4u) & 3u, pop16(), true);
        return 10u;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        const uint16_t a = fetch16();
        const uint8_t cc = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const bool cond = cc == 0u ? !zeroFlag() : cc == 1u ? zeroFlag() : cc == 2u ? ((regF() & kFlagC) == 0u) : cc == 3u ? ((regF() & kFlagC) != 0u) : cc == 4u ? ((regF() & kFlagPV) == 0u) : cc == 5u ? ((regF() & kFlagPV) != 0u) : cc == 6u ? ((regF() & kFlagS) == 0u) : ((regF() & kFlagS) != 0u);
        if (cond) PC = a;
        return 10u;
    }
    case 0xC3: PC = fetch16(); return 10u;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        const uint16_t a = fetch16();
        const uint8_t cc = static_cast<uint8_t>((opcode >> 3u) & 7u);
        const bool cond = cc == 0u ? !zeroFlag() : cc == 1u ? zeroFlag() : cc == 2u ? ((regF() & kFlagC) == 0u) : cc == 3u ? ((regF() & kFlagC) != 0u) : cc == 4u ? ((regF() & kFlagPV) == 0u) : cc == 5u ? ((regF() & kFlagPV) != 0u) : cc == 6u ? ((regF() & kFlagS) == 0u) : ((regF() & kFlagS) != 0u);
        if (cond) { push16(PC); PC = a; return 17u; }
        return 10u;
    }
    case 0xC5: case 0xD5: case 0xE5: case 0xF5:
        push16(getReg16ByPair((opcode >> 4u) & 3u, true));
        return 11u;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        (void)alu(static_cast<uint8_t>((opcode >> 3u) & 7u), fetch8());
        return 7u;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push16(PC);
        PC = static_cast<uint16_t>(opcode & 0x38u);
        return 11u;
    case 0xC9: PC = pop16(); return 10u;
    case 0xCB: return executeCbOpcode(fetchOpcode(), false, 0u, nullptr);
    case 0xCD: { const uint16_t a = fetch16(); push16(PC); PC = a; return 17u; }
    case 0xD3: { const uint8_t p = fetch8(); writeIo(p, regA()); return 11u; }
    case 0xD9: std::swap(BC, BC_); std::swap(DE, DE_); std::swap(HL, HL_); return 4u;
    case 0xDB: setRegA(readIo(fetch8())); return 11u;
    case 0xDD: case 0xFD: return executeIndexedOpcode(opcode, fetchOpcode());
    case 0xE3: { const uint16_t temp = read16(SP); write16(SP, HL); HL = temp; return 19u; }
    case 0xE9: PC = HL; return 4u;
    case 0xEB: std::swap(DE, HL); return 4u;
    case 0xED: return executeEdOpcode(fetchOpcode());
    case 0xF3: IFF1 = IFF2 = IME = false; imeEnableDelay_ = 0; return 4u;
    case 0xF9: SP = HL; return 6u;
    case 0xFB: IFF1 = IFF2 = true; imeEnableDelay_ = 2; return 4u;
    default: return 4u;
    }
}

uint32_t Z80Interpreter::handleInterrupts() {
    if (!interruptRequestProvider || !IME || !IFF1) {
        return 0u;
    }
    auto vector = interruptRequestProvider();
    if (!vector.has_value()) {
        return 0u;
    }
    halted_ = false;
    IFF1 = IFF2 = IME = false;
    imeEnableDelay_ = 0;
    if (interruptMode_ == 2u) {
        push16(PC);
        PC = read16(word(*vector, I));
        return 19u;
    }
    if (interruptMode_ == 0u && (*vector & 0xC7u) == 0xC7u) {
        push16(PC);
        PC = static_cast<uint16_t>(*vector & 0x38u);
        return 11u;
    }
    push16(PC);
    PC = 0x0038u;
    return 11u;
}

uint32_t Z80Interpreter::step() {
    requireMemoryInterface();
    if (const uint32_t interruptCycles = handleInterrupts(); interruptCycles != 0u) {
        return interruptCycles;
    }
    if (halted_) {
        return 4u;
    }
    const uint8_t opcode = fetchOpcode();
    const uint32_t cycles = executeOpcode(opcode);
    if (imeEnableDelay_ > 0) {
        --imeEnableDelay_;
        if (imeEnableDelay_ == 0) {
            IME = IFF1;
        }
    }
    return cycles;
}
