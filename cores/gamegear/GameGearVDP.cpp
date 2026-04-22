#include "GameGearVDP.hpp"

#include <algorithm>
#include <cstddef>

GameGearVDP::GameGearVDP() {}
GameGearVDP::~GameGearVDP() {}

void GameGearVDP::reset() {
    vram_.fill(0u);
    oam_.fill(0u);
    registers_.fill(0u);
    pendingCycles_ = 0u;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    registers_[0x07u] = 0xFCu; // default BGP-like palette for debug renderer compatibility
}

void GameGearVDP::step(uint32_t cpuCycles) {
    if ((registers_[0] & 0x80u) == 0u) {
        registers_[4] = 0u; // LY
        pendingCycles_ = 0u;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
        return;
    }

    pendingCycles_ += cpuCycles;
    while (pendingCycles_ >= kCyclesPerScanline) {
        pendingCycles_ -= kCyclesPerScanline;
        const uint8_t currentLy = registers_[4];
        if (currentLy < kVisibleScanlines) {
            scanlineReadyPending_ = true;
        }

        uint8_t nextLy = static_cast<uint8_t>(currentLy + 1u);
        if (nextLy >= kTotalScanlines) {
            nextLy = 0u;
        }
        registers_[4] = nextLy;
        if (nextLy == kVisibleScanlines) {
            vblankPending_ = true;
        }
    }
}

uint8_t GameGearVDP::readVram(uint16_t address) const {
    const auto index = static_cast<std::size_t>(address - 0x8000u);
    return index < vram_.size() ? vram_[index] : 0xFFu;
}

void GameGearVDP::writeVram(uint16_t address, uint8_t value) {
    const auto index = static_cast<std::size_t>(address - 0x8000u);
    if (index < vram_.size()) {
        vram_[index] = value;
    }
}

uint8_t GameGearVDP::readOam(uint16_t address) const {
    const auto index = static_cast<std::size_t>(address - 0xFE00u);
    return index < oam_.size() ? oam_[index] : 0xFFu;
}

void GameGearVDP::writeOam(uint16_t address, uint8_t value) {
    const auto index = static_cast<std::size_t>(address - 0xFE00u);
    if (index < oam_.size()) {
        oam_[index] = value;
    }
}

uint8_t GameGearVDP::readRegister(uint16_t address) const {
    if (address >= 0xFF40u && address <= 0xFF4Bu) {
        return registers_[static_cast<std::size_t>(address - 0xFF40u)];
    }
    return 0xFFu;
}

void GameGearVDP::writeRegister(uint16_t address, uint8_t value) {
    if (address < 0xFF40u || address > 0xFF4Bu) {
        return;
    }
    registers_[static_cast<std::size_t>(address - 0xFF40u)] = value;
    if (address == 0xFF40u && (value & 0x80u) == 0u) {
        registers_[4] = 0u;
        pendingCycles_ = 0u;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
    }
}

bool GameGearVDP::takeScanlineReady() {
    const bool ready = scanlineReadyPending_;
    scanlineReadyPending_ = false;
    return ready;
}

bool GameGearVDP::takeVBlankEntered() {
    const bool ready = vblankPending_;
    vblankPending_ = false;
    return ready;
}

BMMQ::VideoDebugFrameModel GameGearVDP::buildFrameModel(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    BMMQ::VideoDebugFrameModel model;
    model.width = std::max(request.frameWidth, 1);
    model.height = std::max(request.frameHeight, 1);
    model.displayEnabled = (registers_[0] & 0x80u) != 0u;
    model.inVBlank = registers_[4] >= kVisibleScanlines;
    model.scanlineIndex = registers_[4];
    model.argbPixels.assign(
        static_cast<std::size_t>(model.width) * static_cast<std::size_t>(model.height),
        paletteColor(0u));
    model.semantics.resize(model.argbPixels.size());
    if (!model.displayEnabled) {
        return model;
    }

    const auto palette = registers_[7];
    for (int y = 0; y < model.height; ++y) {
        const auto tileY = static_cast<std::size_t>(y / 8);
        const auto pixelY = static_cast<std::size_t>(y % 8);
        for (int x = 0; x < model.width; ++x) {
            const auto tileX = static_cast<std::size_t>(x / 8);
            const auto pixelX = static_cast<std::size_t>(x % 8);
            const auto tileIndex = tileMapEntry(tileX, tileY);
            const auto colorIndex = sampleTileColor(tileIndex, pixelX, pixelY);
            const auto shade = mapPaletteShade(palette, colorIndex);
            model.argbPixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(model.width)
                             + static_cast<std::size_t>(x)] = paletteColor(shade);
        }
    }

    return model;
}

uint32_t GameGearVDP::paletteColor(uint8_t shade) noexcept {
    switch (shade & 0x03u) {
    case 0u:
        return 0xFFE0F8D0u;
    case 1u:
        return 0xFF88C070u;
    case 2u:
        return 0xFF346856u;
    default:
        return 0xFF081820u;
    }
}

uint8_t GameGearVDP::mapPaletteShade(uint8_t palette, uint8_t colorIndex) noexcept {
    // `sampleTileColor` returns values in 0..3, but defensively mask here
    // to ensure `colorIndex` stays within 2 bits before shifting so we
    // never shift by more than 6 and never read undefined bits from the
    // 8-bit `palette` parameter.
    const uint8_t idx = static_cast<uint8_t>(colorIndex & 0x03u);
    return static_cast<uint8_t>((palette >> (idx * 2u)) & 0x03u);
}

uint8_t GameGearVDP::tileMapEntry(std::size_t tileX, std::size_t tileY) const noexcept {
    const auto wrappedTileX = tileX % kTilesPerRow;
    const auto wrappedTileY = tileY % kTilesPerRow;
    const auto index = kTileMapOffset + wrappedTileY * kTilesPerRow + wrappedTileX;
    return index < vram_.size() ? vram_[index] : 0u;
}

uint8_t GameGearVDP::sampleTileColor(uint8_t tileIndex, std::size_t pixelX, std::size_t pixelY) const noexcept {
    const auto tileBase = static_cast<std::size_t>(tileIndex) * 16u;
    const auto rowBase = tileBase + (pixelY % 8u) * 2u;
    if (rowBase + 1u >= vram_.size()) {
        return 0u;
    }
    const auto low = vram_[rowBase];
    const auto high = vram_[rowBase + 1u];
    const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(pixelX % 8u));
    return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
}
