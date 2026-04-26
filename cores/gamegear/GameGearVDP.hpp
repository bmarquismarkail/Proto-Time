#pragma once
// Sega Game Gear Video Display Processor (VDP).
// Reference: .internal/docs/vdp-doc.txt (Charles MacDonald SMS/GG VDP notes)

#include <array>
#include <cstdint>
#include "machine/VideoDebugModel.hpp"

class GameGearVDP {
public:
    // ...existing public API...

    // --- Test/Debug: Expose internal state for testing ---
    [[nodiscard]] bool isCommandLatchPending() const noexcept { return commandLatchPending_; }
    [[nodiscard]] bool isFrameInterruptPending() const noexcept { return frameInterruptPending_; }
    [[nodiscard]] bool isIrqAsserted() const noexcept { return irqAsserted_; }
    [[nodiscard]] bool isLineInterruptPending() const noexcept { return lineInterruptPending_; }
    // Test-only: expose raw CRAM bytes (64 bytes / 32 entries)
    [[nodiscard]] const std::array<uint8_t, 0x0040>& debugCram() const noexcept { return cram_; }
    // Test-only: expose decoded CRAM color (ARGB) cache per color index (0..31)
    [[nodiscard]] uint32_t debugDecodedCramColor(std::size_t colorIndex) const noexcept;
public:
    GameGearVDP();
    ~GameGearVDP();

    void reset();
    void setSmsMode(bool enabled) noexcept;
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
    [[nodiscard]] bool takeIrqAsserted();
    [[nodiscard]] uint8_t currentScanline() const noexcept;
    [[nodiscard]] uint8_t lastReadyScanline() const noexcept;
    [[nodiscard]] uint8_t readVCounter() const noexcept;
    [[nodiscard]] uint8_t readHCounter() const noexcept;
    void latchHCounter() noexcept;
    [[nodiscard]] BMMQ::VideoDebugFrameModel buildFrameModel(
        const BMMQ::VideoDebugRenderRequest& request) const;

private:
    static constexpr uint32_t kCyclesPerScanline = 228u;
    static constexpr uint16_t kTotalScanlines = 262u;
    static constexpr std::size_t kTilesPerRow = 32u;
    static constexpr std::size_t kSpriteCount = 40u;
    static constexpr std::size_t kVramSize = 0x4000u;

    [[nodiscard]] static uint32_t paletteColor(uint8_t shade) noexcept;
    [[nodiscard]] static uint32_t colorFromCramWord(uint16_t cramWord) noexcept;
    [[nodiscard]] std::size_t nameTableBase() const noexcept;
    [[nodiscard]] std::size_t spriteAttributeTableBase() const noexcept;
    [[nodiscard]] std::size_t spriteGeneratorBase() const noexcept;
    [[nodiscard]] uint16_t backgroundTileEntry(std::size_t tileX, std::size_t tileY) const noexcept;
    // Variant accepting precomputed name table base and active display lines
    [[nodiscard]] uint16_t backgroundTileEntry(std::size_t tileX,
                                               std::size_t tileY,
                                               std::size_t nameTableBase,
                                               std::size_t activeLines) const noexcept;
    [[nodiscard]] uint8_t samplePatternColor(std::size_t patternBase,
                                             uint16_t tileIndex,
                                             std::size_t pixelX,
                                             std::size_t pixelY) const noexcept;
    [[nodiscard]] uint32_t sampleCramColor(uint8_t paletteSelect, uint8_t colorCode) const noexcept;
    [[nodiscard]] bool displayEnabled() const noexcept;
    [[nodiscard]] bool mode4Enabled() const noexcept;
    [[nodiscard]] std::size_t activeDisplayLines() const noexcept;
    [[nodiscard]] uint8_t vCounterValue() const noexcept;
    [[nodiscard]] bool spriteTallMode() const noexcept;
    [[nodiscard]] bool spriteZoomMode() const noexcept;
    void evaluateScanlineStatus(uint8_t scanline) noexcept;
    void writeCompatRegister(std::size_t index, uint8_t value);
    [[nodiscard]] uint8_t readCompatRegister(std::size_t index) const noexcept;
    void seedDefaultCram();
    // Recompute/refresh decoded palette cache from raw CRAM bytes
    void recomputeDecodedCramCache() noexcept;
    void updateDecodedCramEntry(std::size_t colorIndex) noexcept;
    void recomputeIrqAsserted() noexcept;


    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, 0x00A0> oam_{};
    std::array<uint8_t, 0x000B> registers_{};
    std::array<uint8_t, 0x0040> cram_{};
    // Decoded CRAM cache: one ARGB color per CRAM entry (32 entries)
    std::array<uint32_t, 32> decodedCram_{};
    uint32_t pendingCycles_ = 0u;
    uint16_t scanline_ = 0u;
    uint8_t lastReadyScanline_ = 0u;
    uint8_t hCounter_ = 0u;
    uint8_t latchedHCounter_ = 0u;
    bool hCounterLatched_ = false;
    bool scanlineReadyPending_ = false;
    bool vblankPending_ = false;
    bool frameInterruptPending_ = false;
    bool lineInterruptPending_ = false;
    bool irqAsserted_ = false;
    bool spriteOverflowPending_ = false;
    bool spriteCollisionPending_ = false;
    uint8_t lastStatusScanline_ = 0xFFu;
    bool statusScanlineConsumed_ = true;
    uint16_t dataAddress_ = 0u;
    uint8_t commandLow_ = 0u;
    uint8_t readBuffer_ = 0u;
    uint8_t cramLatch_ = 0u;
    bool cramLatchValid_ = false;
    uint8_t lineCounter_ = 0u;
    uint8_t verticalScrollLatch_ = 0u;
    enum class AccessMode : uint8_t {
        VramRead = 0x00u,
        VramWrite = 0x01u,
        RegisterWrite = 0x02u,
        CramWrite = 0x03u,
    };
    AccessMode accessMode_ = AccessMode::VramRead;
    bool commandLatchPending_ = false;
    bool smsMode_ = false;
};
