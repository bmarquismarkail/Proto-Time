#pragma once
// Sega Game Gear Z80 CPU interpreter stub
// References: SMS Power, MAME, Emulicious, Genesis Plus GX

#include <cstdint>
#include <functional>

class Z80Interpreter {
public:
    using MemRead = std::function<uint8_t(uint16_t)>;
    using MemWrite = std::function<void(uint16_t, uint8_t)>;

    Z80Interpreter();
    ~Z80Interpreter();

    void reset();
    [[nodiscard]] uint32_t step();

    // Register interface
    void setMemoryInterface(MemRead reader, MemWrite writer);

    // Z80 registers
    uint16_t AF = 0;
    uint16_t BC = 0;
    uint16_t DE = 0;
    uint16_t HL = 0;
    uint16_t IX = 0;
    uint16_t IY = 0;
    uint16_t SP = 0;
    uint16_t PC = 0;
    uint16_t AF_ = 0;
    uint16_t BC_ = 0;
    uint16_t DE_ = 0;
    uint16_t HL_ = 0; // shadow registers
    uint8_t I = 0;
    uint8_t R = 0; // Interrupt vector, refresh
    bool IFF1 = false;
    bool IFF2 = false;
    bool IME = false;

private:
    MemRead memRead;
    MemWrite memWrite;

    // Internal helpers
    void requireMemoryInterface() const;
    uint8_t fetch8();
    uint16_t fetch16();
    void executeOpcode(uint8_t opcode);
    void handleInterrupts();
    // TODO: Add full opcode decode/execute tables
};
