#pragma once
#include "../GameGearCartridge.hpp"

// Sega 315-5365 paging chip mapper.
// Behavior: supports up to 32 x 16KB pages. SRAM support is unknown;
// conservatively expose no battery-backed save by default. Use the
// base cartridge behaviour for reads/writes and paging.
class Sega3155365Mapper : public GameGearCartridge {
public:
    Sega3155365Mapper() = default;
    ~Sega3155365Mapper() override = default;

    bool load(const uint8_t* data, size_t size) override {
        return GameGearCartridge::load(data, size);
    }

    void reset() override {
        // No special documented reset; fall back to base behaviour.
        GameGearCartridge::reset();
    }

    [[nodiscard]] bool supportsSaveData() const noexcept override {
        // Documentation is unclear whether 315-5365 provides SRAM support.
        // To avoid falsely enabling save persistence for cartridges that
        // don't have it, report no save support by default. If a concrete
        // board is known to include battery-backed RAM, this can be
        // adjusted or the cartridge can provide its own mapper.
        return false;
    }
};
