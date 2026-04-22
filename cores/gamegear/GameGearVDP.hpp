#pragma once
// Sega Game Gear Video Display Processor (VDP) stub
// References: SMS Power, Charles MacDonald, MAME

#include <array>
#include <cstdint>
#include "machine/VideoDebugModel.hpp"

class GameGearVDP {
public:
    GameGearVDP();
    ~GameGearVDP();

    void reset();
    void step(uint32_t cpuCycles);
    [[nodiscard]] uint8_t readVram(uint16_t address) const;
    void writeVram(uint16_t address, uint8_t value);
    [[nodiscard]] uint8_t readOam(uint16_t address) const;
    void writeOam(uint16_t address, uint8_t value);
    [[nodiscard]] uint8_t readRegister(uint16_t address) const;
    void writeRegister(uint16_t address, uint8_t value);
    [[nodiscard]] bool takeScanlineReady();
    [[nodiscard]] bool takeVBlankEntered();
    [[nodiscard]] BMMQ::VideoDebugFrameModel buildFrameModel(
        const BMMQ::VideoDebugRenderRequest& request) const;

private:
    static constexpr uint32_t kCyclesPerScanline = 228u;
    static constexpr uint8_t kVisibleScanlines = 144u;
    static constexpr uint8_t kTotalScanlines = 154u;
    static constexpr std::size_t kTileMapOffset = 0x1800u;
    static constexpr std::size_t kTilesPerRow = 32u;

    [[nodiscard]] static uint32_t paletteColor(uint8_t shade) noexcept;
    [[nodiscard]] static uint8_t mapPaletteShade(uint8_t palette, uint8_t colorIndex) noexcept;
    [[nodiscard]] uint8_t tileMapEntry(std::size_t tileX, std::size_t tileY) const noexcept;
    [[nodiscard]] uint8_t sampleTileColor(uint8_t tileIndex, std::size_t pixelX, std::size_t pixelY) const noexcept;

    std::array<uint8_t, 0x2000> vram_{};
    std::array<uint8_t, 0x00A0> oam_{};
    std::array<uint8_t, 0x000C> registers_{};
    uint32_t pendingCycles_ = 0u;
    bool scanlineReadyPending_ = false;
    bool vblankPending_ = false;
};
