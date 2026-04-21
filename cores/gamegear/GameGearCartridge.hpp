#pragma once
// Sega Game Gear cartridge interface stub
// References: SMS Power, Charles MacDonald

#include <cstdint>
#include <cstddef>
#include <vector>

class GameGearCartridge {
public:
    GameGearCartridge();
    ~GameGearCartridge();

    bool load(const uint8_t* data, size_t size);
    // TODO: Add MBC/mapper support

private:
    std::vector<uint8_t> rom;
};
