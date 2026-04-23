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
    [[nodiscard]] uint8_t readDataPort();
    [[nodiscard]] uint8_t readControlPort();
    void writeDataPort(uint8_t value);
    void writeControlPort(uint8_t value);
    [[nodiscard]] bool takeScanlineReady();
    [[nodiscard]] bool takeVBlankEntered();
    [[nodiscard]] uint8_t currentScanline() const noexcept;
    [[nodiscard]] BMMQ::VideoDebugFrameModel buildFrameModel(
        const BMMQ::VideoDebugRenderRequest& request) const;

private:
    static constexpr uint32_t kCyclesPerScanline = 228u;
    static constexpr uint8_t kVisibleScanlines = 144u;
    static constexpr uint8_t kTotalScanlines = 154u;
    static constexpr std::size_t kTileMapOffset = 0x1800u;
    static constexpr std::size_t kTilesPerRow = 32u;
    static constexpr std::size_t kSpriteCount = 40u;
    static constexpr std::size_t kVramSize = 0x4000u;

    [[nodiscard]] static uint32_t paletteColor(uint8_t shade) noexcept;
    [[nodiscard]] static uint32_t colorFromCramWord(uint16_t cramWord) noexcept;
    [[nodiscard]] std::size_t nameTableBase() const noexcept;
    [[nodiscard]] std::size_t spriteAttributeTableBase() const noexcept;
    [[nodiscard]] std::size_t spriteGeneratorBase() const noexcept;
    [[nodiscard]] uint16_t backgroundTileEntry(std::size_t tileX, std::size_t tileY) const noexcept;
    [[nodiscard]] uint8_t samplePatternColor(std::size_t patternBase,
                                             uint16_t tileIndex,
                                             std::size_t pixelX,
                                             std::size_t pixelY) const noexcept;
    [[nodiscard]] uint32_t sampleCramColor(uint8_t paletteSelect, uint8_t colorCode) const noexcept;
    [[nodiscard]] bool displayEnabled() const noexcept;
    [[nodiscard]] bool spriteTallMode() const noexcept;
    void writeCompatRegister(std::size_t index, uint8_t value);
    [[nodiscard]] uint8_t readCompatRegister(std::size_t index) const noexcept;
    void seedDefaultCram();

    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, 0x00A0> oam_{};
    std::array<uint8_t, 0x000C> registers_{};
    std::array<uint8_t, 0x0040> cram_{};
    uint32_t pendingCycles_ = 0u;
    bool scanlineReadyPending_ = false;
    bool vblankPending_ = false;
    uint16_t dataAddress_ = 0u;
    uint8_t commandLow_ = 0u;
    uint8_t readBuffer_ = 0u;
    enum class AccessMode : uint8_t {
        VramRead = 0x00u,
        VramWrite = 0x01u,
        RegisterWrite = 0x02u,
        CramWrite = 0x03u,
    };
    AccessMode accessMode_ = AccessMode::VramRead;
    bool commandLatchPending_ = false;
};
