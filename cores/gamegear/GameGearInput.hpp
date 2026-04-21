#pragma once
// Sega Game Gear input subsystem stub
// References: SMS Power, MAME

#include <cstdint>

class GameGearInput {
public:
    GameGearInput();
    ~GameGearInput();

    void reset();
    uint8_t readInputs();
    // TODO: Add input mapping interface
};
