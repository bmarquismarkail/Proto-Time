#include "GameBoyPPU.hpp"
#include "GameBoyMemoryMap.hpp"
#include "gameboy.hpp"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace GB {

GameBoyPPU::GameBoyPPU() : dotCounter_(0), ly_(0), ppuMode_(kModeDMATransfer) {
    scanlineCaptureModel_.width = kDisplayWidth;
    scanlineCaptureModel_.height = kDisplayHeight;
    scanlineCaptureModel_.argbPixels.resize(kFramePixelCount);
    scanlineBackgroundColors_.resize(static_cast<std::size_t>(kDisplayWidth));
}

void GameBoyPPU::reset() {
    dotCounter_ = 0;
    ly_ = 0;
    ppuMode_ = kModeDMATransfer;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    lastReadyScanline_ = 0;
    framePixels_.fill(paletteColor(0));
    frameColorIndices_.fill(0u);
    capturedScanlines_.fill(false);
    hasCapturedScanlines_ = false;
    lcdEnabledLastStep_ = false;
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

uint8_t GameBoyPPU::paletteIndex(uint32_t argb) noexcept {
    for (uint8_t shade = 0u; shade < 4u; ++shade) {
        if (paletteColor(shade) == argb) {
            return shade;
        }
    }
    return 0u;
}

uint8_t GameBoyPPU::mapPaletteShade(uint8_t paletteReg, uint8_t colorIndex) noexcept {
    return static_cast<uint8_t>((paletteReg >> (colorIndex * 2u)) & 0x03u);
}

uint8_t GameBoyPPU::readVram(uint16_t address) const {
    if (cpu) return cpu->read_vram(address);
    if (!memoryMap) return 0xFFu;
    return memoryMap->read(address);
}

uint8_t GameBoyPPU::readOam(uint16_t address) const {
    if (!memoryMap) return 0xFFu;
    return memoryMap->read(address);
}

GameBoyPPU::BackgroundSample GameBoyPPU::sampleBackground(int screenX, int screenY,
                                                          uint8_t lcdc, uint8_t scy, uint8_t scx,
                                                          uint8_t wy, uint8_t wx) const {
    if (!memoryMap) return {};

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

    std::sort(candidates.begin(), candidates.end(),
              [](const SpriteCandidate& a, const SpriteCandidate& b) {
                  if (a.spriteX != b.spriteX) return a.spriteX > b.spriteX;
                  return a.oamIndex > b.oamIndex;
              });

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
    const uint8_t scy = memoryMap->read(0xFF42);
    const uint8_t scx = memoryMap->read(0xFF43);
    const uint8_t backgroundPalette = memoryMap->read(0xFF47);
    const uint8_t wy = memoryMap->read(0xFF4A);
    const uint8_t wx = memoryMap->read(0xFF4B);
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

        auto sample = sampleBackground(x, screenY, lcdc, scy, scx, wy, wx);
        bgColors[static_cast<std::size_t>(x)] = sample.colorIndex;

        uint8_t shade = mapPaletteShade(backgroundPalette, sample.colorIndex);
        model.argbPixels[static_cast<std::size_t>(pixelIndex)] = paletteColor(shade);
    }

    if (cpu) cpu->set_sprite_context(1);
    compositeSprites(model, screenY, bgColors);
    if (cpu) cpu->set_sprite_context(0);
}

void GameBoyPPU::captureScanline(int screenY) {
    if (screenY < 0 || screenY >= kDisplayHeight) {
        return;
    }

    renderScanline(scanlineCaptureModel_, screenY, scanlineBackgroundColors_);

    const auto rowOffset = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(kDisplayWidth);
    std::copy_n(scanlineCaptureModel_.argbPixels.begin() + static_cast<std::ptrdiff_t>(rowOffset),
                static_cast<std::size_t>(kDisplayWidth),
                framePixels_.begin() + static_cast<std::ptrdiff_t>(rowOffset));
    for (std::size_t x = 0u; x < static_cast<std::size_t>(kDisplayWidth); ++x) {
        frameColorIndices_[rowOffset + x] = paletteIndex(scanlineCaptureModel_.argbPixels[rowOffset + x]);
    }
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

BMMQ::RealtimeVideoSubmission GameBoyPPU::buildRealtimeFrame(const BMMQ::VideoDebugRenderRequest& request) const {
    BMMQ::RealtimeVideoPacket packet;
    packet.contractVersion = BMMQ::RealtimeVideoPacket::kContractVersion;
    packet.width = std::max(request.frameWidth, 1);
    packet.height = std::max(request.frameHeight, 1);
    packet.displayEnabled = memoryMap != nullptr && (memoryMap->read(0xFF40u) & 0x80u) != 0u;
    packet.inVBlank = ly_ >= 144u;
    packet.scanlineIndex = ly_;

    static constexpr std::array<std::uint32_t, 4u> kPalette{
        0xFFE0F8D0u, 0xFF88C070u, 0xFF346856u, 0xFF081820u,
    };
    if (packet.width == kDisplayWidth && packet.height == kDisplayHeight && hasCapturedScanlines_) {
        packet.surface = BMMQ::makeIndexedVideoSurface(
            frameColorIndices_, packet.width, packet.height,
            BMMQ::RealtimeVideoEncoding::Indexed2, kPalette);
    } else {
        auto model = buildFrameModel(request);
        std::vector<std::uint8_t> indices(model.argbPixels.size(), 0u);
        std::transform(model.argbPixels.begin(), model.argbPixels.end(), indices.begin(), paletteIndex);
        packet.surface = BMMQ::makeIndexedVideoSurface(
            indices, packet.width, packet.height,
            BMMQ::RealtimeVideoEncoding::Indexed2, kPalette);
    }
    packet.uploadHints[0] = {0u, 0u, static_cast<std::uint16_t>(packet.width),
                             static_cast<std::uint16_t>(packet.height)};
    packet.uploadHintCount = 1u;
    return BMMQ::RealtimeVideoSubmission{.packet = std::move(packet)};
}

void GameBoyPPU::step(uint32_t cpuCycles) {
    if (!memoryMap) return;

    uint8_t lcdc = memoryMap->read(0xFF40);
    if ((lcdc & 0x80u) == 0u) {
        // LCD disabled resets LY.
        dotCounter_ = 0;
        ly_ = 0;
        ppuMode_ = kModeHBlank;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
        lastReadyScanline_ = 0;
        if (lcdEnabledLastStep_) {
            framePixels_.fill(paletteColor(0));
            frameColorIndices_.fill(0u);
            capturedScanlines_.fill(false);
            hasCapturedScanlines_ = false;
        }
        lcdEnabledLastStep_ = false;
        lastLy_ = 0;
        return;
    }
    lcdEnabledLastStep_ = true;

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

// Save state export/import.
std::vector<uint8_t> GameBoyPPU::exportState() const {
    std::vector<uint8_t> state;
    const auto appendU32 = [&state](uint32_t value) {
        state.push_back(static_cast<uint8_t>(value & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };
    appendU32(dotCounter_);
    state.push_back(static_cast<uint8_t>(ppuMode_));
    state.push_back(ly_);
    state.push_back(lastReadyScanline_);
    state.push_back(lastLy_);
    state.push_back(static_cast<uint8_t>(scanlineReadyPending_));
    state.push_back(static_cast<uint8_t>(vblankPending_));
    return state;
}

void GameBoyPPU::importState(const std::vector<uint8_t>& state) {
    if (state.size() != 10u) {
        throw std::invalid_argument("PPU state too short");
    }
    const auto readU32 = [&state](std::size_t offset) {
        return static_cast<uint32_t>(state[offset]) |
               (static_cast<uint32_t>(state[offset + 1u]) << 8u) |
               (static_cast<uint32_t>(state[offset + 2u]) << 16u) |
               (static_cast<uint32_t>(state[offset + 3u]) << 24u);
    };
    const uint32_t nextDotCounter = readU32(0u);
    const uint8_t nextMode = state[4];
    const uint8_t nextLy = state[5];
    const uint8_t nextLastReadyScanline = state[6];
    const uint8_t nextLastLy = state[7];
    const uint8_t nextScanlineReady = state[8];
    const uint8_t nextVblank = state[9];
    if (nextDotCounter >= kCyclesPerScanline ||
        nextMode > kModeDMATransfer ||
        nextLy >= kTotalScanlines ||
        nextLastReadyScanline >= kTotalScanlines ||
        nextLastLy >= kTotalScanlines ||
        nextScanlineReady > 1u ||
        nextVblank > 1u) {
        throw std::invalid_argument("PPU state contains invalid values");
    }

    dotCounter_ = nextDotCounter;
    ppuMode_ = nextMode;
    ly_ = nextLy;
    lastReadyScanline_ = nextLastReadyScanline;
    lastLy_ = nextLastLy;
    scanlineReadyPending_ = nextScanlineReady != 0u;
    vblankPending_ = nextVblank != 0u;

    // Clear captured scanlines for deterministic mid-frame restore
    capturedScanlines_.fill(0);
    hasCapturedScanlines_ = false;
    lcdEnabledLastStep_ = memoryMap != nullptr && (memoryMap->read(0xFF40u) & 0x80u) != 0u;
    if (!lcdEnabledLastStep_) {
        framePixels_.fill(paletteColor(0));
        frameColorIndices_.fill(0u);
    }
}

} // namespace GB
