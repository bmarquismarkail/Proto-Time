#pragma once
#include "../GameGearCartridge.hpp"

// Sega 315-5235 paging chip mapper.
// Implements chip-specific reset state and relies on the
// base GameGearCartridge for banking/SRAM behaviour.
class Sega3155235Mapper : public GameGearCartridge {
public:
    Sega3155235Mapper() = default;
    ~Sega3155235Mapper() override = default;

    bool load(const uint8_t* data, size_t size) override {
        return GameGearCartridge::load(data, size);
    }

    void reset() override {
        // Base reset establishes a sane default; then apply the
        // documented 315-5235 power-up register state:
        // FFFC=0x00, FFFD=0x00, FFFE=0x01, FFFF=0x02
        GameGearCartridge::reset();
        write(0xFFFCu, 0x00u);
        write(0xFFFDu, 0x00u);
        write(0xFFFEu, 0x01u);
        write(0xFFFFu, 0x02u);
    }
};
