#include "GameGearVDP.hpp"

#include <algorithm>
#include <cstddef>

GameGearVDP::GameGearVDP() {}
GameGearVDP::~GameGearVDP() {}

void GameGearVDP::reset() {
    vram_.fill(0u);
    oam_.fill(0u);
    registers_.fill(0u);
    cram_.fill(0u);
    pendingCycles_ = 0u;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    registers_[0x02u] = 0xFFu; // default pattern name table at 0x3800
    registers_[0x05u] = 0xFFu; // default SAT at 0x3F00
    registers_[0x06u] = 0xFFu; // default sprite generator at 0x2000
    registers_[0x07u] = 0x00u; // backdrop color code
    dataAddress_ = 0u;
    commandLow_ = 0u;
    readBuffer_ = 0u;
    accessMode_ = AccessMode::VramRead;
    commandLatchPending_ = false;
    seedDefaultCram();
}

void GameGearVDP::step(uint32_t cpuCycles) {
    if (!displayEnabled()) {
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
    if (index >= oam_.size()) {
        return 0xFFu;
    }
    const auto spriteIndex = index / 4u;
    const auto field = index % 4u;
    const auto satBase = spriteAttributeTableBase();
    if (spriteIndex >= 64u || satBase >= vram_.size()) {
        return oam_[index];
    }
    if (field == 0u) {
        return vram_[satBase + spriteIndex];
    }
    if (field == 1u) {
        const auto xBase = (satBase + 0x80u) % vram_.size();
        return vram_[(xBase + spriteIndex * 2u) % vram_.size()];
    }
    if (field == 2u) {
        const auto xBase = (satBase + 0x80u) % vram_.size();
        return vram_[(xBase + spriteIndex * 2u + 1u) % vram_.size()];
    }
    return oam_[index];
}

void GameGearVDP::writeOam(uint16_t address, uint8_t value) {
    const auto index = static_cast<std::size_t>(address - 0xFE00u);
    if (index < oam_.size()) {
        oam_[index] = value;
        const auto spriteIndex = index / 4u;
        const auto field = index % 4u;
        const auto satBase = spriteAttributeTableBase();
        if (spriteIndex < 64u && satBase < vram_.size()) {
            if (field == 0u) {
                vram_[satBase + spriteIndex] = value;
            } else if (field == 1u) {
                const auto xBase = (satBase + 0x80u) % vram_.size();
                vram_[(xBase + spriteIndex * 2u) % vram_.size()] = value;
            } else if (field == 2u) {
                const auto xBase = (satBase + 0x80u) % vram_.size();
                vram_[(xBase + spriteIndex * 2u + 1u) % vram_.size()] = value;
            }
        }
    }
}

uint8_t GameGearVDP::readRegister(uint16_t address) const {
    if (address >= 0xFF40u && address <= 0xFF4Bu) {
        return readCompatRegister(static_cast<std::size_t>(address - 0xFF40u));
    }
    return 0xFFu;
}

void GameGearVDP::writeRegister(uint16_t address, uint8_t value) {
    if (address < 0xFF40u || address > 0xFF4Bu) {
        return;
    }
    writeCompatRegister(static_cast<std::size_t>(address - 0xFF40u), value);
    if ((address == 0xFF40u || address == 0xFF41u) && !displayEnabled()) {
        registers_[4] = 0u;
        pendingCycles_ = 0u;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
    }
}

uint8_t GameGearVDP::readDataPort() {
    commandLatchPending_ = false;
    const auto value = readBuffer_;
    readBuffer_ = vram_[static_cast<std::size_t>(dataAddress_ % vram_.size())];
    dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) % vram_.size());
    return value;
}

uint8_t GameGearVDP::readControlPort() {
    commandLatchPending_ = false;
    uint8_t status = 0u;
    if (vblankPending_ || registers_[4] >= kVisibleScanlines) {
        status = static_cast<uint8_t>(status | 0x80u);
    }
    return status;
}

void GameGearVDP::writeDataPort(uint8_t value) {
    commandLatchPending_ = false;
    if (accessMode_ == AccessMode::CramWrite) {
        cram_[static_cast<std::size_t>(dataAddress_ % cram_.size())] = value;
        dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) % cram_.size());
        return;
    }
    vram_[static_cast<std::size_t>(dataAddress_ % vram_.size())] = value;
    dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) % vram_.size());
}

void GameGearVDP::writeControlPort(uint8_t value) {
    if (!commandLatchPending_) {
        commandLow_ = value;
        commandLatchPending_ = true;
        return;
    }

    commandLatchPending_ = false;
    const auto command = static_cast<uint8_t>((value >> 6) & 0x03u);
    if (command == 0x02u) {
        writeCompatRegister(static_cast<std::size_t>(value & 0x0Fu), commandLow_);
        return;
    }

    dataAddress_ = static_cast<uint16_t>((static_cast<uint16_t>(value & 0x3Fu) << 8) | commandLow_);
    if (!vram_.empty()) {
        dataAddress_ = static_cast<uint16_t>(dataAddress_ % vram_.size());
    }
    accessMode_ = static_cast<AccessMode>(command);
    if (accessMode_ == AccessMode::VramRead) {
        if (!vram_.empty()) {
            readBuffer_ = vram_[static_cast<std::size_t>(dataAddress_ % vram_.size())];
            dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) % vram_.size());
        }
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
    model.displayEnabled = displayEnabled();
    model.inVBlank = registers_[4] >= kVisibleScanlines;
    model.scanlineIndex = registers_[4];
    model.argbPixels.assign(
        static_cast<std::size_t>(model.width) * static_cast<std::size_t>(model.height),
        paletteColor(0u));
    model.semantics.resize(model.argbPixels.size());
    if (!model.displayEnabled) {
        const auto backdrop = sampleCramColor(1u, static_cast<uint8_t>(registers_[7u] & 0x0Fu));
        std::fill(model.argbPixels.begin(), model.argbPixels.end(), backdrop);
        return model;
    }

    const auto backdrop = sampleCramColor(1u, static_cast<uint8_t>(registers_[7u] & 0x0Fu));
    std::fill(model.argbPixels.begin(), model.argbPixels.end(), backdrop);
    const auto scrollX = readCompatRegister(8u);
    const auto scrollY = readCompatRegister(9u);
    std::vector<bool> backgroundPriority(model.argbPixels.size(), false);
    for (int y = 0; y < model.height; ++y) {
        const auto scrolledY = static_cast<std::size_t>((y + scrollY) & 0xFF);
        const auto tileY = scrolledY / 8u;
        const auto pixelY = scrolledY % 8u;
        for (int x = 0; x < model.width; ++x) {
            const auto scrolledX = static_cast<std::size_t>((x + scrollX) & 0xFF);
            const auto tileX = scrolledX / 8u;
            const auto pixelX = scrolledX % 8u;
            const auto entry = backgroundTileEntry(tileX, tileY);
            const uint16_t tileIndex = static_cast<uint16_t>(((entry >> 0u) & 0x01FFu));
            const bool flipH = (entry & 0x0200u) != 0u;
            const bool flipV = (entry & 0x0400u) != 0u;
            const bool palette1 = (entry & 0x0800u) != 0u;
            const bool priority = (entry & 0x1000u) != 0u;
            const auto sampleX = flipH ? (7u - pixelX) : pixelX;
            const auto sampleY = flipV ? (7u - pixelY) : pixelY;
            const auto colorCode = samplePatternColor(0u, tileIndex, sampleX, sampleY);
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(model.width)
                                  + static_cast<std::size_t>(x);
            if (colorCode != 0u) {
                model.argbPixels[pixelIndex] = sampleCramColor(palette1 ? 1u : 0u, colorCode);
            }
            backgroundPriority[pixelIndex] = priority && colorCode != 0u;
        }
    }

    const auto satBase = spriteAttributeTableBase();
    const auto spriteBase = spriteGeneratorBase();
    std::array<std::uint8_t, kVisibleScanlines> spritesOnLine{};
    const int spriteHeight = spriteTallMode() ? 16 : 8;
    std::size_t spriteCount = 0u;
    for (std::size_t sprite = 0; sprite < 64u; ++sprite) {
        const auto yIndex = satBase + sprite;
        if (yIndex >= vram_.size()) {
            break;
        }
        const uint8_t rawY = vram_[yIndex];
        if (rawY == 0xD0u) {
            break;
        }
        if (rawY == 0xE0u) {
            continue;
        }
        const auto xyBase = (satBase + 0x80u + sprite * 2u) % vram_.size();
        const int spriteX = static_cast<int>(vram_[xyBase]);
        uint16_t tileIndex = vram_[(xyBase + 1u) % vram_.size()];
        if (spriteTallMode()) {
            tileIndex = static_cast<uint16_t>(tileIndex & 0x00FEu);
        }
        const int spriteY = static_cast<int>(rawY);
        for (int py = 0; py < spriteHeight; ++py) {
            const int screenY = spriteY + py;
            if (screenY < 0 || screenY >= model.height) {
                continue;
            }
            if (screenY >= 0 && screenY < static_cast<int>(kVisibleScanlines) &&
                spritesOnLine[static_cast<std::size_t>(screenY)] >= 8u) {
                continue;
            }
            for (int px = 0; px < 8; ++px) {
                const int screenX = spriteX + px;
                if (screenX < 0 || screenX >= model.width) {
                    continue;
                }
                const auto rowTileOffset = spriteTallMode() && py >= 8 ? 1u : 0u;
                const auto colorCode = samplePatternColor(spriteBase,
                                                          static_cast<uint16_t>(tileIndex + rowTileOffset),
                                                          static_cast<std::size_t>(px),
                                                          static_cast<std::size_t>(py % 8));
                if (colorCode == 0u) {
                    continue;
                }
                const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(model.width)
                                      + static_cast<std::size_t>(screenX);
                if (backgroundPriority[pixelIndex]) {
                    continue;
                }
                model.argbPixels[pixelIndex] = sampleCramColor(1u, colorCode);
            }
            if (screenY >= 0 && screenY < static_cast<int>(kVisibleScanlines)) {
                spritesOnLine[static_cast<std::size_t>(screenY)] =
                    static_cast<std::uint8_t>(spritesOnLine[static_cast<std::size_t>(screenY)] + 1u);
            }
        }
        ++spriteCount;
        if (spriteCount >= kSpriteCount) {
            break;
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

uint32_t GameGearVDP::colorFromCramWord(uint16_t cramWord) noexcept {
    const auto red = static_cast<uint8_t>((cramWord & 0x000Fu) * 17u);
    const auto green = static_cast<uint8_t>(((cramWord >> 4u) & 0x000Fu) * 17u);
    const auto blue = static_cast<uint8_t>(((cramWord >> 8u) & 0x000Fu) * 17u);
    return 0xFF000000u |
           (static_cast<uint32_t>(red) << 16u) |
           (static_cast<uint32_t>(green) << 8u) |
           static_cast<uint32_t>(blue);
}

std::size_t GameGearVDP::nameTableBase() const noexcept {
    const auto reg2 = registers_[2u];
    if (reg2 == 0u) {
        return kTileMapOffset;
    }
    return static_cast<std::size_t>((reg2 & 0x0Eu) << 10u);
}

std::size_t GameGearVDP::spriteAttributeTableBase() const noexcept {
    const auto reg5 = registers_[5u];
    if (reg5 == 0u) {
        return 0x3F00u;
    }
    return static_cast<std::size_t>((reg5 & 0x7Eu) << 7u);
}

std::size_t GameGearVDP::spriteGeneratorBase() const noexcept {
    return (registers_[6u] & 0x04u) != 0u ? 0x2000u : 0x0000u;
}

uint16_t GameGearVDP::backgroundTileEntry(std::size_t tileX, std::size_t tileY) const noexcept {
    const auto wrappedTileX = tileX % kTilesPerRow;
    const auto wrappedTileY = tileY % 28u;
    const auto base = nameTableBase();
    const auto index = base + (wrappedTileY * kTilesPerRow + wrappedTileX) * 2u;
    if (index + 1u >= vram_.size()) {
        return 0u;
    }
    return static_cast<uint16_t>(vram_[index] | (static_cast<uint16_t>(vram_[index + 1u]) << 8u));
}

uint8_t GameGearVDP::samplePatternColor(std::size_t patternBase,
                                        uint16_t tileIndex,
                                        std::size_t pixelX,
                                        std::size_t pixelY) const noexcept {
    const auto tileBase = (patternBase + static_cast<std::size_t>(tileIndex) * 32u) % vram_.size();
    const auto rowBase = (tileBase + (pixelY % 8u) * 4u) % vram_.size();
    const auto plane0 = vram_[rowBase];
    const auto plane1 = vram_[(rowBase + 1u) % vram_.size()];
    const auto plane2 = vram_[(rowBase + 2u) % vram_.size()];
    const auto plane3 = vram_[(rowBase + 3u) % vram_.size()];
    const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(pixelX % 8u));
    return static_cast<uint8_t>((((plane3 >> bit) & 0x01u) << 3u) |
                                (((plane2 >> bit) & 0x01u) << 2u) |
                                (((plane1 >> bit) & 0x01u) << 1u) |
                                ((plane0 >> bit) & 0x01u));
}

uint32_t GameGearVDP::sampleCramColor(uint8_t paletteSelect, uint8_t colorCode) const noexcept {
    const auto colorIndex = static_cast<std::size_t>((paletteSelect & 0x01u) * 16u + (colorCode & 0x0Fu));
    const auto byteIndex = colorIndex * 2u;
    if (byteIndex + 1u >= cram_.size()) {
        return paletteColor(colorCode & 0x03u);
    }
    const auto even = cram_[byteIndex];
    const auto odd = cram_[byteIndex + 1u];
    const auto cramWord = static_cast<uint16_t>((even & 0x0Fu) |
                                                ((even & 0xF0u) << 0u) |
                                                ((odd & 0x0Fu) << 8u));
    return colorFromCramWord(cramWord);
}

bool GameGearVDP::displayEnabled() const noexcept {
    return (registers_[1u] & 0x40u) != 0u || (registers_[0u] & 0x80u) != 0u;
}

bool GameGearVDP::spriteTallMode() const noexcept {
    return (registers_[1u] & 0x02u) != 0u;
}

void GameGearVDP::writeCompatRegister(std::size_t index, uint8_t value) {
    if (index >= registers_.size()) {
        return;
    }
    registers_[index] = value;
}

uint8_t GameGearVDP::readCompatRegister(std::size_t index) const noexcept {
    if (index >= registers_.size()) {
        return 0xFFu;
    }
    return registers_[index];
}

void GameGearVDP::seedDefaultCram() {
    auto setWord = [this](std::size_t colorIndex, uint8_t red, uint8_t green, uint8_t blue) {
        const auto byteIndex = colorIndex * 2u;
        if (byteIndex + 1u >= cram_.size()) {
            return;
        }
        cram_[byteIndex] = static_cast<uint8_t>(((green & 0x0Fu) << 4u) | (red & 0x0Fu));
        cram_[byteIndex + 1u] = static_cast<uint8_t>(blue & 0x0Fu);
    };
    for (std::size_t i = 0; i < 32u; ++i) {
        setWord(i, 0u, 0u, 0u);
    }
    setWord(0u, 14u, 15u, 13u);
    setWord(1u, 8u, 12u, 7u);
    setWord(2u, 3u, 6u, 5u);
    setWord(3u, 0u, 1u, 2u);
    setWord(16u, 14u, 15u, 13u);
    setWord(17u, 15u, 0u, 0u);
    setWord(18u, 0u, 15u, 0u);
    setWord(19u, 0u, 0u, 15u);
    setWord(20u, 15u, 15u, 15u);
}
