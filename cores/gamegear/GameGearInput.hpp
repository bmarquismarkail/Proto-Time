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

private:
    BMMQ::InputButtonMask logicalButtons_ = 0u;
};
