#include "GameBoyPPU.hpp"
#include "GameBoyMemoryMap.hpp"
#include <algorithm>
#include <cstring>

namespace GB {

GameBoyPPU::GameBoyPPU() : dotCounter_(0), ly_(0), ppuMode_(kModeDMATransfer) {}

void GameBoyPPU::reset() {
    dotCounter_ = 0;
    ly_ = 0;
    ppuMode_ = kModeDMATransfer;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    lastReadyScanline_ = 0;
    lastLy_ = 0;
}

uint32_t GameBoyPPU::paletteColor(uint8_t shade) noexcept {
    switch (shade & 0x03u) {
    case 0: return 0xFFE0F8D0u; // Lightest
    case 1: return 0xFF88C070u; // Light
    case 2: return 0xFF346856u; // Dark
    default: return 0xFF081820u; // Darkest
    }
}

uint8_t GameBoyPPU::mapPaletteShade(uint8_t paletteReg, uint8_t colorIndex) noexcept {
    return static_cast<uint8_t>((paletteReg >> (colorIndex * 2u)) & 0x03u);
}

uint8_t GameBoyPPU::readVram(uint16_t address) const {
    if (!memoryMap) return 0xFFu;
    return memoryMap->read(address);
}

uint8_t GameBoyPPU::readOam(uint16_t address) const {
    if (!memoryMap) return 0xFFu;
    return memoryMap->read(address);
}

GameBoyPPU::BackgroundSample GameBoyPPU::sampleBackground(int screenX, int screenY) const {
    if (!memoryMap) return {};

    // Read LCDC and STAT from memory map
    uint8_t lcdc = memoryMap->read(0xFF40);
    uint8_t scy = memoryMap->read(0xFF42);
    uint8_t scx = memoryMap->read(0xFF43);
    uint8_t wy = memoryMap->read(0xFF4A);
    uint8_t wx = memoryMap->read(0xFF4B);

    bool windowEnabled = (lcdc & 0x20u) != 0;
    int windowLeft = static_cast<int>(wx) - 7;
    bool useWindow = windowEnabled && screenY >= static_cast<int>(wy) && screenX >= windowLeft;

    int mapX = (screenX + static_cast<int>(scx)) & 0xFF;
    int mapY = (screenY + static_cast<int>(scy)) & 0xFF;
    uint16_t mapBase = (lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;

    if (useWindow) {
        mapBase = (lcdc & 0x40u) != 0u ? 0x9C00u : 0x9800u;
        mapX = std::max(0, screenX - windowLeft);
        mapY = std::max(0, screenY - static_cast<int>(wy));
    }

    auto tileMapAddress = static_cast<uint16_t>(
        mapBase + static_cast<uint16_t>(((mapY >> 3) & 0x1Fu) * 32 + ((mapX >> 3) & 0x1Fu)));
    uint8_t tileIndex = readVram(tileMapAddress);
    bool unsignedTileData = (lcdc & 0x10u) != 0u;
    uint8_t tileX = static_cast<uint8_t>(mapX & 0x07);
    uint8_t tileY = static_cast<uint8_t>(mapY & 0x07);

    uint16_t tileAddress;
    if (unsignedTileData) {
        tileAddress = static_cast<uint16_t>(0x8000u + static_cast<uint16_t>(tileIndex) * 16u);
    } else {
        tileAddress = static_cast<uint16_t>(0x9000u + static_cast<std::int16_t>(static_cast<std::int8_t>(tileIndex)) * 16);
    }

    uint8_t colorIndex = sampleTileColor(tileIndex, unsignedTileData, tileX, tileY);

    return BackgroundSample{
        .tileIndex = tileIndex,
        .tileAddress = tileAddress,
        .tileX = tileX,
        .tileY = tileY,
        .colorIndex = colorIndex,
        .useWindow = useWindow,
        .unsignedTileData = unsignedTileData,
    };
}

uint8_t GameBoyPPU::sampleTileColor(uint8_t tileIndex, bool unsignedTileData,
                                     uint8_t tileX, uint8_t tileY) const {
    uint16_t tileAddress;
    if (unsignedTileData) {
        tileAddress = static_cast<uint16_t>(0x8000u + static_cast<uint16_t>(tileIndex) * 16u);
    } else {
        tileAddress = static_cast<uint16_t>(0x9000u + static_cast<std::int16_t>(static_cast<std::int8_t>(tileIndex)) * 16);
    }

    uint16_t rowAddress = static_cast<uint16_t>(tileAddress + static_cast<uint16_t>(tileY) * 2u);
    uint8_t low = readVram(rowAddress);
    uint8_t high = readVram(static_cast<uint16_t>(rowAddress + 1u));

    uint8_t bit = static_cast<uint8_t>(7u - (tileX & 0x07u));
    return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
}

void GameBoyPPU::compositeSprites(BMMQ::VideoDebugFrameModel& model, int screenY,
                                  std::vector<uint8_t>& bgColors) const {
    if (!memoryMap) return;

    uint8_t lcdc = memoryMap->read(0xFF40);
    if ((lcdc & 0x02u) == 0u) return; // Sprites disabled

    bool tallSprites = (lcdc & 0x04u) != 0u;
    int spriteHeight = tallSprites ? 16 : 8;

    for (int spriteIndex = 39; spriteIndex >= 0; --spriteIndex) {
        uint16_t base = static_cast<uint16_t>(0xFE00u + static_cast<uint16_t>(spriteIndex) * 4u);
        int spriteY = static_cast<int>(readOam(base)) - 16;
        int spriteX = static_cast<int>(readOam(static_cast<uint16_t>(base + 1u))) - 8;
        uint8_t tileIndex = readOam(static_cast<uint16_t>(base + 2u));
        uint8_t attributes = readOam(static_cast<uint16_t>(base + 3u));

        if (spriteX <= -8 || spriteX >= model.width ||
            spriteY <= -spriteHeight || spriteY >= kDisplayHeight) {
            continue;
        }
        if (screenY < spriteY || screenY >= spriteY + spriteHeight) {
            continue;
        }

        if (tallSprites) {
            tileIndex = static_cast<uint8_t>(tileIndex & 0xFEu);
        }

        bool xFlip = (attributes & 0x20u) != 0u;
        bool yFlip = (attributes & 0x40u) != 0u;
        bool behindBackground = (attributes & 0x80u) != 0u;
        uint8_t palette = (attributes & 0x10u) != 0u
            ? memoryMap->read(0xFF49) // OBP1
            : memoryMap->read(0xFF48); // OBP0

        int localY = screenY - spriteY;
        int spriteRow = yFlip ? (spriteHeight - 1 - localY) : localY;
        uint8_t effectiveTile = tileIndex;
        if (tallSprites && spriteRow >= 8) {
            effectiveTile = static_cast<uint8_t>(tileIndex + 1u);
            spriteRow -= 8;
        }

        for (int localX = 0; localX < 8; ++localX) {
            int screenX = spriteX + localX;
            if (screenX < 0 || screenX >= static_cast<int>(bgColors.size())) continue;

            uint8_t spriteColumn = static_cast<uint8_t>(xFlip ? (7u - static_cast<uint8_t>(localX)) : static_cast<uint8_t>(localX));
            uint8_t colorIndex = sampleTileColor(effectiveTile, true, spriteColumn, static_cast<uint8_t>(spriteRow));

            if (colorIndex == 0u) continue;
            if (behindBackground && bgColors[static_cast<std::size_t>(screenX)] != 0u) continue;

            // Overwrite background pixel
            int pixelIndex = screenY * static_cast<int>(model.width) + screenX;
            if (pixelIndex < 0 || pixelIndex >= static_cast<int>(model.argbPixels.size())) {
                continue;
            }
            uint8_t shade = mapPaletteShade(palette, colorIndex);
            model.argbPixels[static_cast<std::size_t>(pixelIndex)] = paletteColor(shade);
        }
    }
}

void GameBoyPPU::renderScanline(BMMQ::VideoDebugFrameModel& model, int screenY,
                                 std::vector<uint8_t>& bgColors) const {
    if (!memoryMap) return;

    if (screenY < 0 || screenY >= kDisplayHeight || model.width <= 0) return;

    uint8_t lcdc = memoryMap->read(0xFF40);
    bool lcdEnabled = (lcdc & 0x80u) != 0u;
    bool backgroundEnabled = (lcdc & 0x01u) != 0u;

    if (!bgColors.size()) {
        bgColors.resize(static_cast<std::size_t>(model.width), 0u);
    }
    std::fill(bgColors.begin(), bgColors.end(), 0u);

    for (int x = 0; x < model.width && x < kDisplayWidth; ++x) {
        int pixelIndex = screenY * static_cast<int>(model.width) + x;
        if (pixelIndex >= static_cast<int>(model.argbPixels.size())) break;

        if (!lcdEnabled || !backgroundEnabled) {
            model.argbPixels[static_cast<std::size_t>(pixelIndex)] = paletteColor(0);
            continue;
        }

        auto sample = sampleBackground(x, screenY);
        bgColors[static_cast<std::size_t>(x)] = sample.colorIndex;

        uint8_t shade = mapPaletteShade(memoryMap->read(0xFF47), sample.colorIndex);
        model.argbPixels[static_cast<std::size_t>(pixelIndex)] = paletteColor(shade);
    }

    compositeSprites(model, screenY, bgColors);
}

BMMQ::VideoDebugFrameModel GameBoyPPU::buildFrameModel(const BMMQ::VideoDebugRenderRequest& request) const {
    BMMQ::VideoDebugFrameModel model;
    model.width = std::max(request.frameWidth, 1);
    model.height = std::max(request.frameHeight, 1);

    if (!memoryMap) return model;

    uint8_t lcdc = memoryMap->read(0xFF40);
    model.displayEnabled = (lcdc & 0x80u) != 0u;
    model.inVBlank = (ly_ >= 144u);
    model.scanlineIndex = ly_;

    model.argbPixels.assign(static_cast<std::size_t>(model.width) * static_cast<std::size_t>(model.height),
                            paletteColor(0));

    std::vector<uint8_t> bgColors;
    for (int y = 0; y < model.height; ++y) {
        renderScanline(model, y, bgColors);
    }

    return model;
}

BMMQ::RealtimeVideoPacket GameBoyPPU::buildRealtimeFrame(const BMMQ::VideoDebugRenderRequest& request) const {
    auto model = buildFrameModel(request);

    BMMQ::RealtimeVideoPacket packet;
    packet.contractVersion = BMMQ::RealtimeVideoPacket::kContractVersion;
    packet.width = model.width;
    packet.height = model.height;
    packet.displayEnabled = model.displayEnabled;
    packet.inVBlank = model.inVBlank;
    packet.scanlineIndex = model.scanlineIndex;
    packet.argbPixels = std::move(model.argbPixels);
    return packet;
}

void GameBoyPPU::step(uint32_t cpuCycles) {
    if (!memoryMap) return;

    uint8_t lcdc = memoryMap->read(0xFF40);
    if ((lcdc & 0x80u) == 0u) {
        // LCD disabled resets LY.
        ly_ = 0;
        ppuMode_ = kModeDMATransfer;
        return;
    }

    dotCounter_ += cpuCycles;

    while (dotCounter_ >= kCyclesPerScanline) {
        dotCounter_ -= kCyclesPerScanline;

        // Emit scanline ready event for visible scanlines
        if (ly_ < 144u) {
            scanlineReadyPending_ = true;
            lastReadyScanline_ = ly_;
        }

        // VBlank entry
        if (ly_ == 143u) {
            vblankPending_ = true;
        }

        // Advance LY
        ly_++;
        if (ly_ >= static_cast<uint8_t>(kTotalScanlines)) {
            ly_ = 0;
            ppuMode_ = kModeDMATransfer;
        } else if (ly_ < 144u) {
            ppuMode_ = kModeDMATransfer; // Will transition through modes during scanline
        } else {
            ppuMode_ = kModeVBlank;
        }

        lastLy_ = ly_;
    }

    // Sub-scanline mode transitions (simplified)
    // Mode 3: cycles 0-79 (sprite search + pixel processing)
    // Mode 2: cycles 80-167 (OAM search)
    // Mode 0: cycles 172-455 (HBlank)
    // Mode 1: entire VBlank scanlines (144-152)
    if (ly_ < 144u && dotCounter_ < kCyclesPerScanline) {
        uint32_t dotPos = dotCounter_;
        if (dotPos < 80u) {
            ppuMode_ = kModeSpriteSearch;
        } else if (dotPos < 168u) {
            ppuMode_ = kModeDMATransfer;
        } else {
            ppuMode_ = kModeHBlank;
        }
    }
}

bool GameBoyPPU::takeScanlineReady() {
    if (!scanlineReadyPending_) return false;
    scanlineReadyPending_ = false;
    return true;
}

bool GameBoyPPU::takeVBlankEntered() {
    if (!vblankPending_) return false;
    vblankPending_ = false;
    return true;
}

} // namespace GB
