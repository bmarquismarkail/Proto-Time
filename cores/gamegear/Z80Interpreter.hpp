#pragma once
// Sega Game Gear Z80 CPU interpreter stub
// References: SMS Power, MAME, Emulicious, Genesis Plus GX

#include <cstdint>
#include <functional>
#include <optional>

class Z80Interpreter {
public:
    using MemRead = std::function<uint8_t(uint16_t)>;
    using MemWrite = std::function<void(uint16_t, uint8_t)>;
    using IoRead = std::function<uint8_t(uint8_t)>;
    using IoWrite = std::function<void(uint8_t, uint8_t)>;

    Z80Interpreter();
    ~Z80Interpreter();

    void reset();
    [[nodiscard]] uint32_t step();

    // Register interface
    void setMemoryInterface(MemRead reader, MemWrite writer);
    void setIoInterface(IoRead reader, IoWrite writer);

    // Install a provider that returns true when an external IRQ should be
    // serviced. The provider MUST atomically clear the pending request when
    // it returns true; the interpreter will call it from the CPU thread.
    void setInterruptRequestProvider(std::function<std::optional<uint8_t>()> provider);

    // Programmatic setter for interrupt mode (0,1,2). Default is 1.
    void setInterruptMode(uint8_t mode) { interruptMode_ = mode; }

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
    // Set by EI; becomes effective only after the instruction following
    // the `EI` instruction completes (deferred IME enable semantics).
    bool imeEnablePending_ = false;

private:
    // Z80 F register flags (canonical bit positions):
    // S  Z  -  H  -  P/V  N  C
    // 7  6  5  4  3    2  1  0 (bit indices)
    static constexpr uint8_t kFlagS = 0x80u;   // Sign (bit 7)
    static constexpr uint8_t kFlagZ = 0x40u;   // Zero (bit 6)
    static constexpr uint8_t kFlagH = 0x10u;   // Half-carry (bit 4)
    static constexpr uint8_t kFlagPV = 0x04u;  // Parity/Overflow (bit 2)
    static constexpr uint8_t kFlagN = 0x02u;   // Add/Subtract (bit 1)
    static constexpr uint8_t kFlagC = 0x01u;   // Carry (bit 0)

    MemRead memRead;
    MemWrite memWrite;
    IoRead ioRead;
    IoWrite ioWrite;

    // Internal helpers
    void requireMemoryInterface() const;
    uint8_t readIo(uint8_t port) const;
    void writeIo(uint8_t port, uint8_t value) const;
    uint8_t fetch8();
    uint16_t fetch16();
    [[nodiscard]] uint8_t regA() const noexcept;
    void setRegA(uint8_t value) noexcept;
    [[nodiscard]] uint8_t regF() const noexcept;
    void setRegF(uint8_t value) noexcept;
    void setZeroFlag(bool set) noexcept;
    [[nodiscard]] bool zeroFlag() const noexcept;
    [[nodiscard]] uint32_t executeOpcode(uint8_t opcode);
    // Handle any external interrupts. Returns consumed cycles (0 if none).
    uint32_t handleInterrupts();
    // TODO: Add full opcode decode/execute tables
    // Interrupt request provider: returns true if a pending external IRQ
    // should be serviced; the provider is expected to atomically consume
    // the request when returning true.
    std::function<std::optional<uint8_t>()> interruptRequestProvider;
    // Interrupt mode (0,1,2). Defaults to IM1.
    uint8_t interruptMode_ = 1u;
};
