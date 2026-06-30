#include "GameBoyInput.hpp"

#include <stdexcept>
#include <vector>

namespace GB {

void GameBoyInput::reset() {
    logicalButtons_ = 0x00;
    joypRegister_ = 0xCF; // Neither group selected, bits 0-3 read unpressed.
}

void GameBoyInput::setLogicalButtons(uint8_t mask) {
    logicalButtons_ = mask;
}

uint8_t GameBoyInput::getPhysicalState() const noexcept {
    return joypRegister_;
}

uint8_t GameBoyInput::readRegister() const noexcept {
    // Per Pan Docs: bits 0-3 are active low, bits 6-7 reserved high
    // Bit 4: D-pad select (0=dir, 1=buttons)
    // Bit 5: Button select (0=buttons, 1=dir)
    // When a direction/button is pressed, its bit reads as 0
    uint8_t result = static_cast<uint8_t>(0xC0u | (joypRegister_ & 0x30u) | 0x0Fu);

    bool dirSelected = (joypRegister_ & 0x10u) == 0; // Bit 4 = 0 means D-pad selected
    bool btnSelected = (joypRegister_ & 0x20u) == 0; // Bit 5 = 0 means buttons selected

    if (dirSelected) {
        // D-pad: bits 0-3 correspond to Right/Left/Up/Down
        if (logicalButtons_ & 0x01u) result &= static_cast<uint8_t>(~0x01u);
        if (logicalButtons_ & 0x02u) result &= static_cast<uint8_t>(~0x02u);
        if (logicalButtons_ & 0x04u) result &= static_cast<uint8_t>(~0x04u);
        if (logicalButtons_ & 0x08u) result &= static_cast<uint8_t>(~0x08u);
    }

    if (btnSelected) {
        // Buttons: A(0x10), B(0x20), Select(0x40), Start(0x80)
        if (logicalButtons_ & 0x10u) result &= static_cast<uint8_t>(~0x01u);
        if (logicalButtons_ & 0x20u) result &= static_cast<uint8_t>(~0x02u);
        if (logicalButtons_ & 0x40u) result &= static_cast<uint8_t>(~0x04u);
        if (logicalButtons_ & 0x80u) result &= static_cast<uint8_t>(~0x08u);
    }

    return result;
}

void GameBoyInput::writeRegister(uint8_t value) noexcept {
    // Only bits 4-5 are writable
    joypRegister_ = (joypRegister_ & 0xCFu) | (value & 0x30u);
}

// Save state export/import.
std::vector<uint8_t> GameBoyInput::exportState() const {
    std::vector<uint8_t> state;
    state.push_back(logicalButtons_);
    state.push_back(joypRegister_);
    return state;
}

void GameBoyInput::importState(const std::vector<uint8_t>& state) {
    if (state.size() != 2u) {
        throw std::invalid_argument("Input state must be exactly 2 bytes");
    }
    logicalButtons_ = state[0];
    joypRegister_ = static_cast<uint8_t>(0xC0u | (state[1] & 0x30u) | 0x0Fu);
}

} // namespace GB
