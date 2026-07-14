#pragma once
// Game Boy PPU (LCD/Video) abstraction.
// References: Pan Docs (LCDC, STAT, LY, scroll/window, sprites)
//
// Responsibilities:
//   - PPU mode state machine (Mode 0-3)
//   - Background/window rendering
//   - Sprite rendering and compositing
//   - Scanline/VBlank event emission
//   - Video frame model and realtime packet building
//   - Timing stats for render path attribution

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "../../machine/VideoDebugModel.hpp"
#include "../../machine/plugins/IoPlugin.hpp"

class LR3592_DMG;

namespace GB {

class GameBoyMemoryMap;

class GameBoyPPU {
public:
    GameBoyMemoryMap* memoryMap = nullptr;
    LR3592_DMG* cpu = nullptr;

    GameBoyPPU();
    ~GameBoyPPU() = default;

    void reset();

    // Advance PPU by CPU cycles (Pan Docs: 4 cycles per dot)
    void step(uint32_t cpuCycles);

    // Event consumption (like GameGearVDP::takeScanlineReady)
    [[nodiscard]] bool takeScanlineReady();
    [[nodiscard]] bool takeVBlankEntered();

    // Status queries
    [[nodiscard]] uint8_t ly() const noexcept { return ly_; }
    [[nodiscard]] uint8_t currentMode() const noexcept { return ppuMode_; }
    [[nodiscard]] uint8_t lastReadyScanline() const noexcept { return lastReadyScanline_; }
    [[nodiscard]] uint8_t currentScanline() const noexcept { return ly_; }

    // Video output
    [[nodiscard]] BMMQ::VideoDebugFrameModel buildFrameModel(
        const BMMQ::VideoDebugRenderRequest& request) const;
    [[nodiscard]] BMMQ::RealtimeVideoSubmission buildRealtimeFrame(
        const BMMQ::VideoDebugRenderRequest& request) const;

    // Save state export/import.
    std::vector<uint8_t> exportState() const;
    void importState(const std::vector<uint8_t>& state);

private:
    // PPU Mode constants (Pan Docs)
    static constexpr uint8_t kModeHBlank = 0;
    static constexpr uint8_t kModeVBlank = 1;
    static constexpr uint8_t kModeSpriteSearch = 2;
    static constexpr uint8_t kModeDMATransfer = 3;

    // Display dimensions
    static constexpr int kDisplayWidth = 160;
    static constexpr int kDisplayHeight = 144;
    static constexpr int kTotalScanlines = 154; // 144 visible + 10 VBlank (LY 144-153)
    static constexpr uint32_t kCyclesPerScanline = 456u;
    static constexpr uint32_t kScanlineCaptureCycle = 80u; // End of mode 2 sprite search/start of pixel transfer.
    static constexpr uint32_t kCyclesPerDot = 4u;
    static constexpr std::size_t kFramePixelCount =
        static_cast<std::size_t>(kDisplayWidth) * static_cast<std::size_t>(kDisplayHeight);

    // Palette color lookup (DMG 4-shade grayscale)
    [[nodiscard]] static uint32_t paletteColor(uint8_t shade) noexcept;
    [[nodiscard]] static uint8_t paletteIndex(uint32_t argb) noexcept;

    // Map palette shade to index based on register
    [[nodiscard]] static uint8_t mapPaletteShade(uint8_t paletteReg, uint8_t colorIndex) noexcept;

    // VRAM read helpers
    [[nodiscard]] uint8_t readVram(uint16_t address) const;
    [[nodiscard]] uint8_t readOam(uint16_t address) const;

    // Background/window sampling
    struct BackgroundSample {
        uint8_t tileIndex = 0;
        uint16_t tileAddress = 0x8000u;
        uint8_t tileX = 0;
        uint8_t tileY = 0;
        uint8_t colorIndex = 0;
        bool useWindow = false;
        bool unsignedTileData = true;
    };
    [[nodiscard]] BackgroundSample sampleBackground(int screenX, int screenY) const;
    [[nodiscard]] uint8_t sampleTileColor(uint8_t tileIndex, bool unsignedTileData,
                                          uint8_t tileX, uint8_t tileY) const;

    // Sprite compositing for a scanline
    void compositeSprites(BMMQ::VideoDebugFrameModel& model, int screenY,
                          std::vector<uint8_t>& bgColors) const;

    // Render a single scanline
    void renderScanline(BMMQ::VideoDebugFrameModel& model, int screenY,
                        std::vector<uint8_t>& bgColors) const;
    void captureScanline(int screenY);

    // Timing state
    uint32_t dotCounter_ = 0;
    uint8_t ly_ = 0;
    uint8_t ppuMode_ = kModeDMATransfer; // Starts in Mode 3 at power-on

    // Event flags
    bool scanlineReadyPending_ = false;
    bool vblankPending_ = false;
    uint8_t lastReadyScanline_ = 0;
    std::array<uint32_t, kFramePixelCount> framePixels_{};
    std::array<uint8_t, kFramePixelCount> frameColorIndices_{};
    std::array<bool, kDisplayHeight> capturedScanlines_{};
    bool hasCapturedScanlines_ = false;

    // Previous LY for transition detection
    uint8_t lastLy_ = 0;
};

} // namespace GB
