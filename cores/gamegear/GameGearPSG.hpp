#pragma once
// Sega Game Gear Programmable Sound Generator (PSG) stub
// References: SMS Power, MAME, Emulicious

#include <cstdint>

class GameGearPSG {
public:
    GameGearPSG();
    ~GameGearPSG();

    void reset();
    void step();
    // TODO: Add audio register interface
};
