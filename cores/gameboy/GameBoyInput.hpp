#pragma once
// Game Boy joypad input abstraction.
// References: Pan Docs (JOYP register at FF00)
//
// Responsibilities:
//   - Logical button state (D-pad + A/B/Select/Start)
//   - Physical state mirroring to JOYP register
//   - Directional vs button select bit control

#include <cstdint>

namespace GB {

class GameBoyInput {
public:
    GameBoyInput() = default;
    ~GameBoyInput() = default;

    void reset();

    // Set logical button state (from frontend/input service)
    void setLogicalButtons(uint8_t mask);

    // Get physical JOYP register state
    [[nodiscard]] uint8_t getPhysicalState() const noexcept;

    // Direct JOYP register access (for memory map intercept)
    [[nodiscard]] uint8_t readRegister() const noexcept;
    void writeRegister(uint8_t value) noexcept;

private:
    // Bit masks for logical buttons
    static constexpr uint8_t kRight  = 0x01;
    static constexpr uint8_t kLeft   = 0x02;
    static constexpr uint8_t kUp     = 0x04;
    static constexpr uint8_t kDown   = 0x08;
    static constexpr uint8_t kA      = 0x10;
    static constexpr uint8_t kB      = 0x20;
    static constexpr uint8_t kSelect = 0x40;
    static constexpr uint8_t kStart  = 0x80;

    // Logical button state (set by frontend)
    uint8_t logicalButtons_ = 0x00;

    // Physical JOYP register bits:
    // Bit 4: D-pad select (0 = directional, 1 = buttons)
    // Bit 5: Button select (0 = buttons, 1 = directional)
    // Bits 0-3: Read only, always 0xF
    // Bits 6-7: Reserved
    uint8_t joypRegister_ = 0xCF; // Default: neither group selected, buttons unpressed.
};

} // namespace GB
