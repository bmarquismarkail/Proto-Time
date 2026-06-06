#pragma once
// Sega Game Gear input subsystem stub
// References: SMS Power, MAME

#include <cstdint>

#include "machine/InputTypes.hpp"

class GameGearInput {
public:
    GameGearInput();
    ~GameGearInput();

    void reset();
    void setLogicalButtons(BMMQ::InputButtonMask mask);
    [[nodiscard]] BMMQ::InputButtonMask logicalButtons() const noexcept;
    uint8_t readInputs();
    [[nodiscard]] uint8_t readSystemPort0() const noexcept;
    [[nodiscard]] uint8_t readSystemPort(uint8_t port) const noexcept;
    void writeSystemPort(uint8_t port, uint8_t value) noexcept;
    [[nodiscard]] uint8_t audioStereoControl() const noexcept;

private:
    BMMQ::InputButtonMask logicalButtons_ = 0u;
    uint8_t extData_ = 0u;
    uint8_t extDirectionNmi_ = 0xFFu;
    uint8_t serialTxData_ = 0u;
    uint8_t serialRxData_ = 0u;
    uint8_t serialControl_ = 0u;
    uint8_t audioStereoControl_ = 0xFFu;
};
