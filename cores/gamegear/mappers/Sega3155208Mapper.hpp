#pragma once
#include "../GameGearCartridge.hpp"

// Thin typed mapper for the Sega 315-5208 paging chip.
// Currently it reuses the behaviour of GameGearCartridge but exists
// as a distinct type so future per-chip behaviour can be implemented.
class Sega3155208Mapper : public GameGearCartridge {
public:
    Sega3155208Mapper() = default;
    ~Sega3155208Mapper() override = default;

    // Inherit behaviour from GameGearCartridge for now. Override only
    // so we can later implement 315-5208-specific reset or quirks.
    bool load(const uint8_t* data, size_t size) override {
        return GameGearCartridge::load(data, size);
    }

    void reset() override {
        // The 315-5208's reset state is historically "undefined" on
        // some revisions. For safety we call base reset; this keeps
        // emulator behaviour consistent with the existing cartridge
        // implementation while allowing future changes here.
        GameGearCartridge::reset();
    }
};
