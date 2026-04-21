#pragma once
// Sega Game Gear memory map stub
// References: SMS Power, Charles MacDonald

#include <cstdint>

class GameGearMemoryMap {
public:
    GameGearMemoryMap();
    ~GameGearMemoryMap();

    void reset();
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    // TODO: Add RAM, ROM, I/O mapping
};
