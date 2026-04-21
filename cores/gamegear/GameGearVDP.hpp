#pragma once
// Sega Game Gear Video Display Processor (VDP) stub
// References: SMS Power, Charles MacDonald, MAME

#include <cstdint>

class GameGearVDP {
public:
    GameGearVDP();
    ~GameGearVDP();

    void reset();
    void step();
    // TODO: Add VRAM, CRAM, register interface
};
