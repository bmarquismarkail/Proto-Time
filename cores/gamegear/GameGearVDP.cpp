#include "GameGearVDP.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

constexpr int kGameGearViewportWidth = 160;
constexpr int kGameGearViewportHeight = 144;
constexpr int kGameGearViewportX = 48;
constexpr int kGameGearViewportY = 24;

[[nodiscard]] int gameGearViewportXOffset(int frameWidth) noexcept
{
    return frameWidth == kGameGearViewportWidth ? kGameGearViewportX : 0;
}

[[nodiscard]] int gameGearTmsViewportX(int frameWidth, int lcdX) noexcept
{
    if (frameWidth == kGameGearViewportWidth) {
        return 8 + ((lcdX * 240) / kGameGearViewportWidth);
    }
    return lcdX;
}

[[nodiscard]] int gameGearViewportYOffset(int frameHeight) noexcept
{
    return frameHeight == kGameGearViewportHeight ? kGameGearViewportY : 0;
}

} // namespace

GameGearVDP::GameGearVDP() {}
GameGearVDP::~GameGearVDP() {}

void GameGearVDP::reset() {
    vram_.fill(0u);
    oam_.fill(0u);
    registers_.fill(0u);
    cram_.fill(0u);
    pendingCycles_ = 0u;
    scanline_ = 0u;
    lastReadyScanline_ = 0u;
    hCounter_ = 0u;
    latchedHCounter_ = 0u;
    hCounterLatched_ = false;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    frameInterruptPending_ = false;
    lineInterruptPending_ = false;
    irqAsserted_ = false;
    spriteOverflowPending_ = false;
    spriteCollisionPending_ = false;
    lastStatusScanline_ = 0xFFu;
    statusScanlineConsumed_ = true;
    registers_[0x02u] = 0xFFu; // default pattern name table at 0x3800
    registers_[0x05u] = 0xFFu; // default SAT at 0x3F00
    registers_[0x06u] = 0xFFu; // default sprite generator at 0x2000
    registers_[0x07u] = 0x00u; // backdrop color code
    dataAddress_ = 0u;
    commandLow_ = 0u;
    readBuffer_ = 0u;
    cramLatch_ = 0u;
    cramLatchValid_ = false;
    lineCounter_ = 0u;
    verticalScrollLatch_ = 0u;
    accessMode_ = AccessMode::VramRead;
    commandLatchPending_ = false;
    seedDefaultCram();
    // Initialize decoded CRAM cache from seeded CRAM
    recomputeDecodedCramCache();

    // Initialize sprite attribute table (Y entries) to 0xE0 (hidden) so
    // default SAT entries do not appear as visible sprites on early
    // scanlines. This matches expected hardware semantics where unused
    // sprite entries are off-screen.
    const auto satBase = spriteAttributeTableBase();
    if (satBase + 64u <= vram_.size()) {
        for (std::size_t i = 0; i < 64u; ++i) {
            vram_[satBase + i] = 0xE0u;
        }
    }
}

void GameGearVDP::setSmsMode(bool enabled) noexcept {
    smsMode_ = enabled;
    // Changing SMS/GG decode semantics requires refreshing decoded cache
    recomputeDecodedCramCache();
}

void GameGearVDP::recomputeIrqAsserted() noexcept {
    const bool vblankEnabled = (registers_[1u] & 0x20u) != 0u;
    const bool lineEnabled = (registers_[0u] & 0x10u) != 0u;
    irqAsserted_ = (frameInterruptPending_ && vblankEnabled) || (lineInterruptPending_ && lineEnabled);
}

void GameGearVDP::step(uint32_t cpuCycles) {
    const bool displayOn = displayEnabled();
    pendingCycles_ += cpuCycles;
    while (pendingCycles_ >= kCyclesPerScanline) {
        pendingCycles_ -= kCyclesPerScanline;
        const uint16_t currentLine = scanline_;
        const auto activeLines = activeDisplayLines();
        const uint8_t currentV = vCounterValue();
        if (displayOn && currentLine < activeLines) {
            evaluateScanlineStatus(currentV);
            lastReadyScanline_ = currentV;
            scanlineReadyPending_ = true;
        }

        scanline_ = static_cast<uint16_t>(scanline_ + 1u);
        if (scanline_ >= kTotalScanlines) {
            scanline_ = 0u;
        }

        if (scanline_ <= activeLines) {
            if (lineCounter_ == 0u) {
                lineCounter_ = registers_[0x0Au];
                lineInterruptPending_ = true;
                recomputeIrqAsserted();
            } else {
                lineCounter_ = static_cast<uint8_t>(lineCounter_ - 1u);
            }
        } else {
            lineCounter_ = registers_[0x0Au];
            verticalScrollLatch_ = registers_[9u];
        }

        if (scanline_ == activeLines + 1u) {
            vblankPending_ = true;
            frameInterruptPending_ = true;
            recomputeIrqAsserted();
        }
    }
    // Update a defensible H counter value (0-255) reflecting position
    // within the current scanline. This is an approximate value suitable
    // for software that polls an H counter.
    hCounter_ = static_cast<uint8_t>((pendingCycles_ * 256u) / kCyclesPerScanline);
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
}

uint8_t GameGearVDP::readDataPort() {
    // Reading data port clears the command latch (see Charles MacDonald VDP docs)
    commandLatchPending_ = false;
    // VRAM reads are buffered: return buffer, then load buffer from VRAM at current address
    const auto value = readBuffer_;
    readBuffer_ = vram_[static_cast<std::size_t>(dataAddress_ & 0x3FFFu)];
    // VDP address auto-increments and wraps at 0x3FFF
    dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) & 0x3FFFu);
    return value;
}

uint8_t GameGearVDP::readControlPort() {
    // Reading control port clears the command latch and status/IRQ flags
    commandLatchPending_ = false;
    // Use the V counter value when evaluating status so the scanline
    // reference matches the value used during step() and sprite evaluation.
    const uint8_t line = readVCounter();
    if (line < activeDisplayLines() && (!statusScanlineConsumed_ || line != lastStatusScanline_)) {
        evaluateScanlineStatus(line);
    }
    uint8_t status = 0u;
    if (frameInterruptPending_) {
        status |= 0x80u;
    }
    if (spriteOverflowPending_) {
        status |= 0x40u;
    }
    if (spriteCollisionPending_) {
        status |= 0x20u;
    }
    // Control-port read clears status/IRQ pending flags (see vdpint.txt)
    frameInterruptPending_ = false;
    lineInterruptPending_ = false;
    spriteOverflowPending_ = false;
    spriteCollisionPending_ = false;
    // Recompute IRQ assertion based on remaining pending flags and enable bits
    recomputeIrqAsserted();
    lastStatusScanline_ = line;
    statusScanlineConsumed_ = true;
    return status;
}

void GameGearVDP::writeDataPort(uint8_t value) {
    // Writing data port clears the command latch
    commandLatchPending_ = false;
    // VRAM writes update the read buffer (see Charles MacDonald VDP docs)
    readBuffer_ = value;
    if (accessMode_ == AccessMode::CramWrite) {
        const auto cramSize = smsMode_ ? 0x20u : cram_.size();
        const auto cramIndex = static_cast<std::size_t>(dataAddress_ % cramSize);
        if (smsMode_) {
            // SMS CRAM: single-byte entries (32 total)
            cram_[cramIndex] = static_cast<uint8_t>(value & 0x3Fu);
            // Update decoded cache for this entry
            updateDecodedCramEntry(cramIndex % decodedCram_.size());
            dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) & 0x3FFFu);
            return;
        }
        if ((cramIndex & 0x01u) == 0u) {
            // Even byte: latch only, do not update decoded cache yet
            cramLatch_ = value;
            cramLatchValid_ = true;
        } else {
            // Odd byte: commit even (if latched) and odd; update decoded cache for color
            const auto evenIndex = cramIndex - 1u;
            if (cramLatchValid_ && evenIndex < cram_.size()) {
                cram_[evenIndex] = cramLatch_;
            }
            cram_[cramIndex] = value;
            // Compute color index (word pairs) and update decoded cache entry
            const auto colorIdx = static_cast<std::size_t>(evenIndex / 2u);
            if (colorIdx < decodedCram_.size()) {
                updateDecodedCramEntry(colorIdx);
            }
        }
        dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) & 0x3FFFu);
        return;
    }
    vram_[static_cast<std::size_t>(dataAddress_ & 0x3FFFu)] = value;
    dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) & 0x3FFFu);
}

void GameGearVDP::writeControlPort(uint8_t value) {
    // Two-byte command latch: first write latches, second write executes
    if (!commandLatchPending_) {
        commandLow_ = value;
        dataAddress_ = static_cast<uint16_t>((dataAddress_ & 0x3F00u) | value);
        commandLatchPending_ = true;
        return;
    }

    commandLatchPending_ = false;
    const auto command = static_cast<uint8_t>((value >> 6) & 0x03u);
    dataAddress_ = static_cast<uint16_t>(((value & 0x3Fu) << 8) | commandLow_);
    dataAddress_ &= 0x3FFFu; // VDP address wraps at 0x3FFF
    accessMode_ = static_cast<AccessMode>(command);
    if (command == 0x02u) {
        writeCompatRegister(static_cast<std::size_t>(value & 0x0Fu), commandLow_);
        return;
    }

    if (accessMode_ == AccessMode::VramRead && !vram_.empty()) {
        readBuffer_ = vram_[static_cast<std::size_t>(dataAddress_ & 0x3FFFu)];
        dataAddress_ = static_cast<uint16_t>((dataAddress_ + 1u) & 0x3FFFu);
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

bool GameGearVDP::takeIrqAsserted() {
    return irqAsserted_;
}

uint8_t GameGearVDP::currentScanline() const noexcept {
    return static_cast<uint8_t>(scanline_ & 0x00FFu);
}

uint8_t GameGearVDP::lastReadyScanline() const noexcept {
    return lastReadyScanline_;
}

uint8_t GameGearVDP::readVCounter() const noexcept {
    return vCounterValue();
}

uint8_t GameGearVDP::readHCounter() const noexcept {
    return hCounterLatched_ ? latchedHCounter_ : hCounter_;
}

void GameGearVDP::latchHCounter() noexcept {
    latchedHCounter_ = hCounter_;
    hCounterLatched_ = true;
}

void GameGearVDP::evaluateScanlineStatus(uint8_t scanline) noexcept {
    lastStatusScanline_ = scanline;
    statusScanlineConsumed_ = false;
    const auto satBase = spriteAttributeTableBase();
    const auto spriteBase = spriteGeneratorBase();
    const int zoom = spriteZoomMode() ? 2 : 1;
    const int spriteHeight = (spriteTallMode() ? 16 : 8) * zoom;
    std::array<bool, 256u> occupied{};
    int spritesOnLine = 0;

    for (std::size_t sprite = 0; sprite < 64u; ++sprite) {
        const auto yIndex = satBase + sprite;
        if (yIndex >= vram_.size()) {
            break;
        }
        const uint8_t rawY = vram_[yIndex];
        if (activeDisplayLines() == 192u && rawY == 0xD0u) {
            break;
        }
        if (rawY == 0xE0u) {
            continue;
        }

        const int spriteY = static_cast<int>(rawY) + 1;
        if (static_cast<int>(scanline) < spriteY || static_cast<int>(scanline) >= spriteY + spriteHeight) {
            continue;
        }

        ++spritesOnLine;
        if (spritesOnLine > 8) {
            spriteOverflowPending_ = true;
        }

        const auto xyBase = (satBase + 0x80u + sprite * 2u) % vram_.size();
        const int spriteX = static_cast<int>(vram_[xyBase]) - ((registers_[0u] & 0x08u) != 0u ? 8 : 0);
        uint16_t tileIndex = vram_[(xyBase + 1u) % vram_.size()];
        if (spriteTallMode()) {
            tileIndex = static_cast<uint16_t>(tileIndex & 0x00FEu);
        }

        const int row = (static_cast<int>(scanline) - spriteY) / zoom;
        const auto rowTileOffset = spriteTallMode() && row >= 8 ? 1u : 0u;
        for (int px = 0; px < 8 * zoom; ++px) {
            const int screenX = spriteX + px;
            if (screenX < 0 || screenX >= static_cast<int>(occupied.size())) {
                continue;
            }
            const auto colorCode = samplePatternColor(spriteBase,
                                                      static_cast<uint16_t>(tileIndex + rowTileOffset),
                                                      static_cast<std::size_t>(px / zoom),
                                                      static_cast<std::size_t>(row % 8));
            (void)colorCode;
            if (colorCode == 0u) {
                continue;
            }
            if (occupied[static_cast<std::size_t>(screenX)]) {
                spriteCollisionPending_ = true;
            } else {
                occupied[static_cast<std::size_t>(screenX)] = true;
            }
        }
    }
}

GameGearVDP::PixelRenderOutput GameGearVDP::renderFramePixels(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    PixelRenderOutput out;
    out.width = std::max(request.frameWidth, 1);
    out.height = std::max(request.frameHeight, 1);
    out.displayEnabled = displayEnabled();
    const auto activeLines = activeDisplayLines();
    const bool mode4 = mode4Enabled();
    const auto nameBasePre = nameTableBase();
    std::array<uint32_t, 16u> backgroundPalette{};
    std::array<uint32_t, 16u> spritePalette{};
    for (std::size_t color = 0u; color < backgroundPalette.size(); ++color) {
        backgroundPalette[color] = decodedCram_[color];
        spritePalette[color] = decodedCram_[16u + color];
    }
    const auto backdrop = spritePalette[static_cast<std::size_t>(registers_[7u] & 0x0Fu)];
    const auto vramSize = vram_.size();
    const bool vramPowerOfTwo = (vramSize != 0u) && ((vramSize & (vramSize - 1u)) == 0u);
    const auto vramMask = vramSize != 0u ? (vramSize - 1u) : 0u;
    auto wrapVram = [&](std::size_t index) noexcept -> std::size_t {
        if (vramSize == 0u) {
            return 0u;
        }
        if (vramPowerOfTwo) {
            return index & vramMask;
        }
        return index % vramSize;
    };
    out.inVBlank = scanline_ >= activeLines + 1u;
    out.scanlineIndex = static_cast<uint8_t>(scanline_ & 0x00FFu);
    const auto pixelCount = static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);
    out.argbPixels.resize(pixelCount);
    if (!out.displayEnabled) {
        std::fill_n(out.argbPixels.data(), pixelCount, backdrop);
        return out;
    }

    if (!mode4 && (registers_[0u] & 0x02u) != 0u) {
        const bool graphicsII = (registers_[0u] & 0x02u) != 0u;
        const auto nameBase = static_cast<std::size_t>((registers_[2u] & 0x0Eu) << 10u);
        const auto patternBase = static_cast<std::size_t>((registers_[4u] & 0x04u) << 11u);
        const auto colorBase = static_cast<std::size_t>((registers_[3u] & 0x80u) << 6u);
        const int viewportY = gameGearViewportYOffset(out.height);
        for (int y = 0; y < out.height; ++y) {
            const auto vdpY = static_cast<std::size_t>(y + viewportY);
            const auto tileY = (vdpY / 8u) % 24u;
            const auto pixelY = vdpY % 8u;
            const auto bankOffset = graphicsII ? ((tileY / 8u) * 0x0800u) : 0u;
            for (int x = 0; x < out.width; ++x) {
                const auto vdpX = static_cast<std::size_t>(gameGearTmsViewportX(out.width, x));
                const auto tileX = (vdpX / 8u) % 32u;
                const auto pixelX = vdpX % 8u;
                const auto nameIndex = wrapVram(nameBase + tileY * 32u + tileX);
                const auto tileIndex = static_cast<std::size_t>(vram_[nameIndex]);
                const auto patternAddress = wrapVram(patternBase + bankOffset + tileIndex * 8u + pixelY);
                const auto colorAddress = wrapVram(colorBase + bankOffset + tileIndex * 8u + pixelY);
                const auto pattern = vram_[patternAddress];
                const auto color = vram_[colorAddress];
                const bool foreground = ((pattern >> (7u - pixelX)) & 0x01u) != 0u;
                const auto colorCode = static_cast<uint8_t>(foreground ? (color >> 4u) : (color & 0x0Fu));
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width)
                                      + static_cast<std::size_t>(x);
                out.argbPixels[pixelIndex] = colorCode == 0u ? backdrop : spritePalette[colorCode & 0x0Fu];
            }
        }
        return out;
    }
    const auto scrollX = readCompatRegister(8u);
    const auto scrollY = verticalScrollLatch_;
    const int viewportX = gameGearViewportXOffset(out.width);
    const int viewportY = gameGearViewportYOffset(out.height);
    const auto satBase = spriteAttributeTableBase();
    const auto spriteBase = spriteGeneratorBase();
    const int zoom = spriteZoomMode() ? 2 : 1;
    const bool tallSprites = spriteTallMode();
    const int spriteHeight = (tallSprites ? 16 : 8) * zoom;
    bool hasVisibleSprites = false;
    std::size_t visibleSpriteProbeCount = 0u;
    for (std::size_t sprite = 0; sprite < 64u; ++sprite) {
        const auto yIndex = satBase + sprite;
        if (yIndex >= vram_.size()) {
            break;
        }
        const uint8_t rawY = vram_[yIndex];
        if (activeLines == 192u && rawY == 0xD0u) {
            break;
        }
        if (rawY == 0xE0u) {
            continue;
        }
        const int spriteY = static_cast<int>(rawY) + 1;
        if (spriteY + spriteHeight > viewportY && spriteY < viewportY + out.height) {
            hasVisibleSprites = true;
            break;
        }
        ++visibleSpriteProbeCount;
        if (visibleSpriteProbeCount >= kSpriteCount) {
            break;
        }
    }
    constexpr uint8_t kSpriteMaskBackgroundPriority = 0x01u;
    constexpr uint8_t kSpriteMaskOccupied = 0x02u;
    thread_local std::vector<uint8_t> spriteMaskScratch;
    auto& spriteMask = spriteMaskScratch;
    if (hasVisibleSprites) {
        spriteMask.assign(out.argbPixels.size(), 0u);
    }
    const bool useSimpleBackgroundPath =
        !smsMode_ &&
        (scrollX & 0x07u) == 0u &&
        (registers_[0u] & 0xA0u) == 0u &&
        !(activeLines == 192u && scrollY > 223u);
    if (!useSimpleBackgroundPath) {
        std::fill_n(out.argbPixels.data(), pixelCount, backdrop);
    }
    thread_local std::vector<std::size_t> simpleTileXsScratch;
    thread_local std::vector<std::size_t> simplePixelXsScratch;
    auto& simpleTileXs = simpleTileXsScratch;
    auto& simplePixelXs = simplePixelXsScratch;
    if (useSimpleBackgroundPath) {
        simpleTileXs.resize(static_cast<std::size_t>(out.width));
        simplePixelXs.resize(static_cast<std::size_t>(out.width));
        const auto startingColumn = static_cast<std::size_t>((32u - (scrollX >> 3u)) & 0x1Fu);
        for (int x = 0; x < out.width; ++x) {
            const auto scrolledX = static_cast<std::size_t>(x + viewportX) & 0xFFu;
            const auto index = static_cast<std::size_t>(x);
            simpleTileXs[index] = (startingColumn + (scrolledX / 8u)) % 32u;
            simplePixelXs[index] = scrolledX % 8u;
        }
    }
    for (int y = 0; y < out.height; ++y) {
        const int vdpY = y + viewportY;

        // Small per-row cache for decoded background tile entries. Decoding the
        // name-table entry once per tile cell avoids repeated VRAM reads in the
        // inner pixel loop and preserves semantics (tile index, flips,
        // palette select, priority).
        struct DecodedBgEntry {
            uint16_t tileIndex = 0u;
            bool flipH = false;
            bool flipV = false;
            bool palette1 = false;
            bool priority = false;
        };
        std::array<DecodedBgEntry, kTilesPerRow> decodedEntries{};
        std::array<std::size_t, kTilesPerRow> decodedEntryRows{};
        std::array<bool, kTilesPerRow> decodedValid{};
        // Per-tile per-row decoded pattern pixels (8 color indexes for the
        // currently decoded sampleY). `decodedPatternRowY` stores the
        // sampleY last decoded for that tile; -1 indicates no row decoded yet.
        std::array<std::array<uint8_t, 8>, kTilesPerRow> decodedPatternRows{};
        std::array<int, kTilesPerRow> decodedPatternRowY{};
        decodedPatternRowY.fill(-1);

        if (useSimpleBackgroundPath) {
            const auto scrolledY = static_cast<std::size_t>((vdpY + scrollY) & 0xFF);
            const auto tileY = scrolledY / 8u;
            const auto pixelY = scrolledY % 8u;
            const auto wrappedTileY = tileY % 32u;
            const auto rowNameBase = nameBasePre + wrappedTileY * kTilesPerRow * 2u;
            for (int x = 0; x < out.width; ++x) {
                const auto index = static_cast<std::size_t>(x);
                const auto tileX = simpleTileXs[index];
                const auto pixelX = simplePixelXs[index];

                if (!decodedValid[tileX] || decodedEntryRows[tileX] != tileY) {
                    const auto entryIndex = rowNameBase + tileX * 2u;
                    const auto entry = entryIndex + 1u < vram_.size()
                        ? static_cast<uint16_t>(vram_[entryIndex] |
                                                (static_cast<uint16_t>(vram_[entryIndex + 1u]) << 8u))
                        : 0u;
                    decodedEntries[tileX].tileIndex = static_cast<uint16_t>(((entry >> 0u) & 0x01FFu));
                    decodedEntries[tileX].flipH = (entry & 0x0200u) != 0u;
                    decodedEntries[tileX].flipV = (entry & 0x0400u) != 0u;
                    decodedEntries[tileX].palette1 = (entry & 0x0800u) != 0u;
                    decodedEntries[tileX].priority = (entry & 0x1000u) != 0u;
                    decodedEntryRows[tileX] = tileY;
                    decodedValid[tileX] = true;
                }
                const auto& decoded = decodedEntries[tileX];
                const auto sampleX = decoded.flipH ? (7u - pixelX) : pixelX;
                const auto sampleY = decoded.flipV ? (7u - pixelY) : pixelY;
                if (decodedPatternRowY[tileX] != static_cast<int>(sampleY)) {
                    const auto tileBase = wrapVram(static_cast<std::size_t>(decoded.tileIndex) * 32u);
                    const auto rowBase = wrapVram(tileBase + static_cast<std::size_t>(sampleY) * 4u);
                    const auto plane0 = vram_[rowBase];
                    const auto plane1 = vram_[wrapVram(rowBase + 1u)];
                    const auto plane2 = vram_[wrapVram(rowBase + 2u)];
                    const auto plane3 = vram_[wrapVram(rowBase + 3u)];
                    for (std::size_t px = 0; px < 8u; ++px) {
                        const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(px));
                        decodedPatternRows[tileX][px] = static_cast<uint8_t>((((plane3 >> bit) & 0x01u) << 3u) |
                                                                              (((plane2 >> bit) & 0x01u) << 2u) |
                                                                              (((plane1 >> bit) & 0x01u) << 1u) |
                                                                              ((plane0 >> bit) & 0x01u));
                    }
                    decodedPatternRowY[tileX] = static_cast<int>(sampleY);
                }
                const auto colorCode = decodedPatternRows[tileX][static_cast<std::size_t>(sampleX)];
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width)
                                      + static_cast<std::size_t>(x);
                out.argbPixels[pixelIndex] = decoded.palette1
                    ? spritePalette[colorCode & 0x0Fu]
                    : backgroundPalette[colorCode & 0x0Fu];
                if (hasVisibleSprites) {
                    spriteMask[pixelIndex] = decoded.priority && colorCode != 0u
                        ? kSpriteMaskBackgroundPriority
                        : 0u;
                }
            }
            continue;
        }

        for (int x = 0; x < out.width; ++x) {
            const int vdpX = x + viewportX;
            const bool fixedTopRows = smsMode_ && (registers_[0u] & 0x40u) != 0u && vdpY < 16;
            const bool fixedRightColumns = (registers_[0u] & 0x80u) != 0u && vdpX >= 192;
            const auto effectiveScrollX = static_cast<uint8_t>(fixedTopRows ? 0u : scrollX);
            const auto effectiveScrollY = static_cast<uint8_t>(fixedRightColumns
                ? 0u
                : (activeLines == 192u && scrollY > 223u ? (scrollY & 0x1Fu) : scrollY));
            if ((registers_[0u] & 0x20u) != 0u && vdpX < 8) {
                continue;
            }
            const auto fineScrollX = static_cast<std::size_t>(effectiveScrollX & 0x07u);
            if (fineScrollX != 0u && vdpX < static_cast<int>(fineScrollX)) {
                continue;
            }
            const auto scrolledY = static_cast<std::size_t>((vdpY + effectiveScrollY) & 0xFF);
            const auto tileY = scrolledY / 8u;
            const auto pixelY = scrolledY % 8u;
            const auto wrappedTileY = tileY % (activeLines == 192u ? 28u : 32u);
            const auto rowNameBase = nameBasePre + wrappedTileY * kTilesPerRow * 2u;
            const auto startingColumn = static_cast<std::size_t>((32u - (effectiveScrollX >> 3u)) & 0x1Fu);
            const auto scrolledX = static_cast<std::size_t>(vdpX - static_cast<int>(fineScrollX)) & 0xFFu;
            const auto tileX = (startingColumn + (scrolledX / 8u)) % 32u;
            const auto pixelX = scrolledX % 8u;

            // Decode the name-table entry at most once per tile cell for this row.
            if (!decodedValid[tileX] || decodedEntryRows[tileX] != tileY) {
                const auto entryIndex = rowNameBase + tileX * 2u;
                const auto entry = entryIndex + 1u < vram_.size()
                    ? static_cast<uint16_t>(vram_[entryIndex] |
                                            (static_cast<uint16_t>(vram_[entryIndex + 1u]) << 8u))
                    : 0u;
                decodedEntries[tileX].tileIndex = static_cast<uint16_t>(((entry >> 0u) & 0x01FFu));
                decodedEntries[tileX].flipH = (entry & 0x0200u) != 0u;
                decodedEntries[tileX].flipV = (entry & 0x0400u) != 0u;
                decodedEntries[tileX].palette1 = (entry & 0x0800u) != 0u;
                decodedEntries[tileX].priority = (entry & 0x1000u) != 0u;
                decodedEntryRows[tileX] = tileY;
                decodedValid[tileX] = true;
            }
            const auto &decoded = decodedEntries[tileX];
            const auto sampleX = decoded.flipH ? (7u - pixelX) : pixelX;
            const auto sampleY = decoded.flipV ? (7u - pixelY) : pixelY;

            // Decode the 4 bitplane bytes for this tile row once and reuse
            // the resulting 8 color indexes for pixels in the tile.
            if (decodedPatternRowY[tileX] != static_cast<int>(sampleY)) {
                const auto tileBase = wrapVram(static_cast<std::size_t>(decoded.tileIndex) * 32u);
                const auto rowBase = wrapVram(tileBase + static_cast<std::size_t>(sampleY) * 4u);
                const auto plane0 = vram_[rowBase];
                const auto plane1 = vram_[wrapVram(rowBase + 1u)];
                const auto plane2 = vram_[wrapVram(rowBase + 2u)];
                const auto plane3 = vram_[wrapVram(rowBase + 3u)];
                for (std::size_t px = 0; px < 8u; ++px) {
                    const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(px));
                    decodedPatternRows[tileX][px] = static_cast<uint8_t>((((plane3 >> bit) & 0x01u) << 3u) |
                                                                          (((plane2 >> bit) & 0x01u) << 2u) |
                                                                          (((plane1 >> bit) & 0x01u) << 1u) |
                                                                          ((plane0 >> bit) & 0x01u));
                }
                decodedPatternRowY[tileX] = static_cast<int>(sampleY);
            }
            const auto colorCode = decodedPatternRows[tileX][static_cast<std::size_t>(sampleX)];
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width)
                                  + static_cast<std::size_t>(x);
            out.argbPixels[pixelIndex] = decoded.palette1
                ? spritePalette[colorCode & 0x0Fu]
                : backgroundPalette[colorCode & 0x0Fu];
            if (hasVisibleSprites) {
                spriteMask[pixelIndex] = decoded.priority && colorCode != 0u
                    ? kSpriteMaskBackgroundPriority
                    : 0u;
            }
        }
    }

    if (!hasVisibleSprites) {
        return out;
    }

    std::array<std::uint8_t, 240u> spritesOnLine{};
    std::size_t spriteCount = 0u;
    for (std::size_t sprite = 0; sprite < 64u; ++sprite) {
        const auto yIndex = satBase + sprite;
        if (yIndex >= vram_.size()) {
            break;
        }
        const uint8_t rawY = vram_[yIndex];
        if (activeLines == 192u && rawY == 0xD0u) {
            break;
        }
        if (rawY == 0xE0u) {
            continue;
        }
        const auto xyBase = wrapVram(satBase + 0x80u + sprite * 2u);
        const int spriteX = static_cast<int>(vram_[xyBase]) - ((registers_[0u] & 0x08u) != 0u ? 8 : 0);
        uint16_t tileIndex = vram_[wrapVram(xyBase + 1u)];
        if (tallSprites) {
            tileIndex = static_cast<uint16_t>(tileIndex & 0x00FEu);
        }
        const int spriteY = static_cast<int>(rawY) + 1;
        const int spriteScreenX = spriteX - viewportX;
        const int spriteDrawWidth = 8 * zoom;
        if (spriteScreenX + spriteDrawWidth <= 0 || spriteScreenX >= out.width) {
            continue;
        }
        for (int py = 0; py < spriteHeight; ++py) {
            const int screenY = spriteY + py - viewportY;
            if (screenY < 0 || screenY >= out.height) {
                continue;
            }
            if (screenY >= 0 && screenY < static_cast<int>(spritesOnLine.size()) &&
                spritesOnLine[static_cast<std::size_t>(screenY)] >= 8u) {
                continue;
            }
            const auto rowTileOffset = tallSprites && py >= 8 ? 1u : 0u;
            const auto rowTileIndex = static_cast<uint16_t>(tileIndex + rowTileOffset);
            const auto rowSampleY = static_cast<std::size_t>((py / zoom) % 8);
            const auto rowTileBase = wrapVram(spriteBase + static_cast<std::size_t>(rowTileIndex) * 32u);
            const auto rowBase = wrapVram(rowTileBase + rowSampleY * 4u);
            const auto plane0 = vram_[rowBase];
            const auto plane1 = vram_[wrapVram(rowBase + 1u)];
            const auto plane2 = vram_[wrapVram(rowBase + 2u)];
            const auto plane3 = vram_[wrapVram(rowBase + 3u)];
            std::array<uint8_t, 8u> spriteRowColors{};
            for (std::size_t sampleX = 0u; sampleX < 8u; ++sampleX) {
                const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(sampleX));
                spriteRowColors[sampleX] = static_cast<uint8_t>((((plane3 >> bit) & 0x01u) << 3u) |
                                                                 (((plane2 >> bit) & 0x01u) << 2u) |
                                                                 (((plane1 >> bit) & 0x01u) << 1u) |
                                                                 ((plane0 >> bit) & 0x01u));
            }
            for (int sampleX = 0; sampleX < 8; ++sampleX) {
                const auto colorCode = spriteRowColors[static_cast<std::size_t>(sampleX)];
                if (colorCode == 0u) {
                    continue;
                }
                const int baseScreenX = spriteScreenX + sampleX * zoom;
                for (int repeat = 0; repeat < zoom; ++repeat) {
                    const int screenX = baseScreenX + repeat;
                    if (screenX < 0 || screenX >= out.width) {
                        continue;
                    }
                    const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(out.width)
                                          + static_cast<std::size_t>(screenX);
                    if ((spriteMask[pixelIndex] &
                         (kSpriteMaskBackgroundPriority | kSpriteMaskOccupied)) != 0u) {
                        continue;
                    }
                    out.argbPixels[pixelIndex] = spritePalette[colorCode & 0x0Fu];
                    spriteMask[pixelIndex] |= kSpriteMaskOccupied;
                }
            }
            if (screenY >= 0 && screenY < static_cast<int>(spritesOnLine.size())) {
                spritesOnLine[static_cast<std::size_t>(screenY)] =
                    static_cast<std::uint8_t>(spritesOnLine[static_cast<std::size_t>(screenY)] + 1u);
            }
        }
        ++spriteCount;
        if (spriteCount >= kSpriteCount) {
            break;
        }
    }

    return out;
}

BMMQ::VideoDebugFrameModel GameGearVDP::buildFrameModel(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    auto out = renderFramePixels(request);
    BMMQ::VideoDebugFrameModel model;
    model.width = out.width;
    model.height = out.height;
    model.displayEnabled = out.displayEnabled;
    model.inVBlank = out.inVBlank;
    model.scanlineIndex = out.scanlineIndex;
    model.argbPixels = std::move(out.argbPixels);
    return model;
}

BMMQ::RealtimeVideoPacket GameGearVDP::buildRealtimeFrame(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    auto out = renderFramePixels(request);
    BMMQ::RealtimeVideoPacket packet;
    packet.width = out.width;
    packet.height = out.height;
    packet.displayEnabled = out.displayEnabled;
    packet.inVBlank = out.inVBlank;
    packet.scanlineIndex = out.scanlineIndex;
    packet.argbPixels = std::move(out.argbPixels);
    return packet;
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

void GameGearVDP::updateDecodedCramEntry(std::size_t colorIndex) noexcept {
    // Ensure index within 0..31
    const auto idx = colorIndex % decodedCram_.size();
    if (smsMode_) {
        const auto smsIndex = idx % 0x20u;
        if (smsIndex < cram_.size()) {
            const auto smsColor = cram_[smsIndex];
            const auto red = static_cast<uint8_t>((smsColor & 0x03u) * 85u);
            const auto green = static_cast<uint8_t>(((smsColor >> 2u) & 0x03u) * 85u);
            const auto blue = static_cast<uint8_t>(((smsColor >> 4u) & 0x03u) * 85u);
            decodedCram_[idx] = 0xFF000000u |
                               (static_cast<uint32_t>(red) << 16u) |
                               (static_cast<uint32_t>(green) << 8u) |
                               static_cast<uint32_t>(blue);
            return;
        }
        decodedCram_[idx] = paletteColor(static_cast<uint8_t>(idx & 0x03u));
        return;
    }
    const auto byteIndex = idx * 2u;
    if (byteIndex + 1u >= cram_.size()) {
        decodedCram_[idx] = paletteColor(static_cast<uint8_t>(idx & 0x03u));
        return;
    }
    const auto even = cram_[byteIndex];
    const auto odd = cram_[byteIndex + 1u];
    const auto cramWord = static_cast<uint16_t>((even & 0x0Fu) |
                                                ((even & 0xF0u) << 0u) |
                                                ((odd & 0x0Fu) << 8u));
    decodedCram_[idx] = colorFromCramWord(cramWord);
}

void GameGearVDP::recomputeDecodedCramCache() noexcept {
    for (std::size_t i = 0; i < decodedCram_.size(); ++i) {
        updateDecodedCramEntry(i);
    }
}

std::size_t GameGearVDP::nameTableBase() const noexcept {
    const auto reg2 = registers_[2u];
    if (mode4Enabled() && activeDisplayLines() != 192u) {
        return static_cast<std::size_t>(0x0700u + (((reg2 >> 2u) & 0x03u) * 0x1000u));
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
    return backgroundTileEntry(tileX, tileY, nameTableBase(), activeDisplayLines());
}

uint16_t GameGearVDP::backgroundTileEntry(std::size_t tileX,
                                         std::size_t tileY,
                                         std::size_t nameTableBase,
                                         std::size_t activeLines) const noexcept
{
    const auto wrappedTileX = tileX % kTilesPerRow;
    const auto wrappedTileY = tileY % (activeLines == 192u ? 28u : 32u);
    const auto index = nameTableBase + (wrappedTileY * kTilesPerRow + wrappedTileX) * 2u;
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
    if (colorIndex >= decodedCram_.size()) {
        return paletteColor(colorCode & 0x03u);
    }
    return decodedCram_[colorIndex];
}

uint32_t GameGearVDP::debugDecodedCramColor(std::size_t colorIndex) const noexcept {
    return decodedCram_[colorIndex % decodedCram_.size()];
}

bool GameGearVDP::displayEnabled() const noexcept {
    return (registers_[1u] & 0x40u) != 0u;
}

bool GameGearVDP::mode4Enabled() const noexcept {
    return (registers_[0u] & 0x04u) != 0u;
}

std::size_t GameGearVDP::activeDisplayLines() const noexcept {
    if (smsMode_ && mode4Enabled() && (registers_[0u] & 0x02u) != 0u) {
        const bool m1 = (registers_[1u] & 0x10u) != 0u;
        const bool m3 = (registers_[1u] & 0x08u) != 0u;
        if (m1 && !m3) {
            return 224u;
        }
        if (!m1 && m3) {
            return 240u;
        }
    }
    return 192u;
}

uint8_t GameGearVDP::vCounterValue() const noexcept {
    if (activeDisplayLines() == 224u) {
        return scanline_ <= 0xEAu ? static_cast<uint8_t>(scanline_) : static_cast<uint8_t>(scanline_ - 6u);
    }
    if (activeDisplayLines() == 240u) {
        return static_cast<uint8_t>(scanline_ & 0x00FFu);
    }
    return scanline_ <= 0xDAu ? static_cast<uint8_t>(scanline_) : static_cast<uint8_t>(scanline_ - 6u);
}

bool GameGearVDP::spriteTallMode() const noexcept {
    return (registers_[1u] & 0x02u) != 0u;
}

bool GameGearVDP::spriteZoomMode() const noexcept {
    return (registers_[1u] & 0x01u) != 0u;
}

void GameGearVDP::writeCompatRegister(std::size_t index, uint8_t value) {
    if (index >= registers_.size()) {
        return;
    }
    registers_[index] = value;
    if (index == 9u && scanline_ > activeDisplayLines()) {
        verticalScrollLatch_ = value;
    }
    if (index == 0x0Au && scanline_ == 0u) {
        lineCounter_ = value;
    }
    // Re-evaluate IRQ assertion when registers change (IE bits may affect state)
    recomputeIrqAsserted();
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
