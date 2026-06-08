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
    framePixels_.fill(paletteColor(0));
    capturedScanlines_.fill(false);
    hasCapturedScanlines_ = false;
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

    // Collect candidate sprites that intersect the current scanline (up to 10)
    struct SpriteCandidate {
        int oamIndex;
        int spriteX;
        uint8_t tileIndex;
        uint8_t attributes;
    };
    std::vector<SpriteCandidate> candidates;

    for (int spriteIndex = 0; spriteIndex < 40; ++spriteIndex) {
        if (static_cast<int>(candidates.size()) >= 10) break;

        auto base = static_cast<uint16_t>(0xFE00u + static_cast<uint16_t>(spriteIndex) * 4u);
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

        candidates.push_back({spriteIndex, spriteX, tileIndex, attributes});
    }

    // Sort candidates by priority: X descending, then OAM index descending
    // This ensures lower priority sprites (higher X or higher OAM index) are drawn first
    std::sort(candidates.begin(), candidates.end(),
              [](const SpriteCandidate& a, const SpriteCandidate& b) {
                  if (a.spriteX != b.spriteX) return a.spriteX > b.spriteX;
                  return a.oamIndex > b.oamIndex;
              });

    // Render candidate sprites in sorted order
    for (const auto& candidate : candidates) {
        int spriteIndex = candidate.oamIndex;
        int spriteX = candidate.spriteX;
        uint8_t tileIndex = candidate.tileIndex;
        uint8_t attributes = candidate.attributes;

        if (tallSprites) {
            tileIndex = static_cast<uint8_t>(tileIndex & 0xFEu);
        }

        bool xFlip = (attributes & 0x20u) != 0u;
        bool yFlip = (attributes & 0x40u) != 0u;
        bool behindBackground = (attributes & 0x80u) != 0u;
        uint8_t palette = (attributes & 0x10u) != 0u
            ? memoryMap->read(0xFF49) // OBP1
            : memoryMap->read(0xFF48); // OBP0

        auto base = static_cast<uint16_t>(0xFE00u + static_cast<uint16_t>(spriteIndex) * 4u);
        int spriteY = static_cast<int>(readOam(base)) - 16;
        int localY = screenY - spriteY;
        int spriteRow = yFlip ? (spriteHeight - 1 - localY) : localY;
        uint8_t effectiveTile = tileIndex;
        if (tallSprites && spriteRow >= 8) {
            effectiveTile = static_cast<uint8_t>(tileIndex + 1u);
            spriteRow -= 8;
        }

        for (int localX = 0; localX < 8; ++localX) {
            int screenX = spriteX + localX;
            if (screenX < 0 || screenX >= static_cast<int>(bgColors.size())) {
                continue;
            }

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

void GameBoyPPU::captureScanline(int screenY) {
    if (screenY < 0 || screenY >= kDisplayHeight) {
        return;
    }

    BMMQ::VideoDebugFrameModel model;
    model.width = kDisplayWidth;
    model.height = kDisplayHeight;
    model.argbPixels.assign(kFramePixelCount, paletteColor(0));

    std::vector<uint8_t> bgColors;
    renderScanline(model, screenY, bgColors);

    const auto rowOffset = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(kDisplayWidth);
    std::copy_n(model.argbPixels.begin() + static_cast<std::ptrdiff_t>(rowOffset),
                static_cast<std::size_t>(kDisplayWidth),
                framePixels_.begin() + static_cast<std::ptrdiff_t>(rowOffset));
    capturedScanlines_[static_cast<std::size_t>(screenY)] = true;
    hasCapturedScanlines_ = true;
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

    if (hasCapturedScanlines_) {
        for (int y = 0; y < model.height && y < kDisplayHeight; ++y) {
            if (!capturedScanlines_[static_cast<std::size_t>(y)]) {
                continue;
            }
            for (int x = 0; x < model.width && x < kDisplayWidth; ++x) {
                const auto sourceIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(kDisplayWidth)
                                       + static_cast<std::size_t>(x);
                const auto targetIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(model.width)
                                       + static_cast<std::size_t>(x);
                model.argbPixels[targetIndex] = framePixels_[sourceIndex];
            }
        }
    }

    std::vector<uint8_t> bgColors;
    for (int y = 0; y < model.height; ++y) {
        if (y < kDisplayHeight && capturedScanlines_[static_cast<std::size_t>(y)]) {
            continue;
        }
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
        framePixels_.fill(paletteColor(0));
        capturedScanlines_.fill(false);
        hasCapturedScanlines_ = false;
        return;
    }

    dotCounter_ += cpuCycles;

    while (true) {
        if (ly_ < 144u &&
            !capturedScanlines_[static_cast<std::size_t>(ly_)] &&
            dotCounter_ >= kScanlineCaptureCycle) {
            captureScanline(ly_);
        }

        if (dotCounter_ < kCyclesPerScanline) {
            break;
        }

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
            capturedScanlines_.fill(false);
            hasCapturedScanlines_ = false;
        } else if (ly_ < 144u) {
            ppuMode_ = kModeDMATransfer; // Will transition through modes during scanline
        } else {
            ppuMode_ = kModeVBlank;
        }

        lastLy_ = ly_;
    }

    // Sub-scanline mode transitions (simplified)
    // Mode 2: cycles 0-79 (OAM search)
    // Mode 3: cycles 80-251 (pixel transfer, simplified fixed length)
    // Mode 0: cycles 252-455 (HBlank)
    // Mode 1: entire VBlank scanlines (144-153)
    if (ly_ < 144u && dotCounter_ < kCyclesPerScanline) {
        uint32_t dotPos = dotCounter_;
        if (dotPos < 80u) {
            ppuMode_ = kModeSpriteSearch;
        } else if (dotPos < 252u) {
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
