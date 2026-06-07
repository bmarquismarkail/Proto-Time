#include "GameBoyMapper.hpp"
#include <algorithm>
#include <cstring>

namespace GB {

GameBoyMapper::GameBoyMapper() {}

void GameBoyMapper::reset() {
    romBankLow_ = 1u;
    effectiveRomBank_ = 1u;
    ramBankSelect_ = 0u;
    ramBankMode_ = 0u;
    ramEnabled_ = false;
    dirty_ = false;
}

void GameBoyMapper::load(const std::vector<uint8_t>& romData) {
    romData_ = romData;
    romSize_ = romData_.size();

    auto meta = parseCartridgeMetadata(romData);
    mapperType_ = meta.mapper;
    hasBattery_ = meta.hasBattery;

    // Calculate ROM bank count (each bank is 0x4000 bytes)
    romBankCount_ = romSize_ / 0x4000u;
    if (romBankCount_ < 2u) romBankCount_ = 2u;

    // External RAM
    externalRamSize_ = meta.externalRamSize;
    if (externalRamSize_ > 0) {
        externalRam_.assign(externalRamSize_, 0xFF);
    }

    // Default: ROM bank 1 (bank 0 is fixed at $0000-$3FFF)
    romBankLow_ = 1u;
    effectiveRomBank_ = 1u;
    ramBankSelect_ = 0u;
    ramBankMode_ = 0u;
    ramEnabled_ = false;
    dirty_ = false;
}

GameBoyMapper::WriteResult GameBoyMapper::write(uint16_t address, uint8_t value) {
    WriteResult result{false, false};

    switch (mapperType_) {
    case CartridgeMapper::None:
        // No MBC — $A000-$BFFF may have RAM
        if (address >= 0xA000u && address < 0xC000u) {
            result.handled = true;
        }
        break;

    case CartridgeMapper::MBC1:
        if (address < 0x2000u) {
            // Bank enable (must be 0x0A for write, 0x00 for read)
            ramEnabled_ = ((value & 0x0Fu) == 0x0Au);
            result.handled = true;
        } else if (address < 0x4000u) {
            // ROM bank low bits (bits 0-4)
            romBankLow_ = (value & 0x1Fu);
            if (romBankLow_ == 0u) romBankLow_ = 1u; // Bank 0 forbidden
            updateMbc1Banking();
            result.romBankChanged = true;
            result.handled = true;
        } else if (address < 0x6000u) {
            // RAM bank select or ROM bank bits 5-6
            if (ramBankMode_ == 0u) {
                ramBankSelect_ = value & 0x03u;
            } else {
                romBankLow_ = (romBankLow_ & 0x01Fu) | ((value & 0x03u) << 5u);
                updateMbc1Banking();
                result.romBankChanged = true;
            }
            result.handled = true;
        } else if (address < 0x8000u) {
            // ROM bank mode select
            ramBankMode_ = value & 0x01u;
            result.handled = true;
        }
        break;

    case CartridgeMapper::MBC2:
        if (address < 0x2000u) {
            // ROM bank select (lower 4 bits of address, value bits 0-3)
            if ((address & 0x1000u) != 0) {
                romBankLow_ = value & 0x0Fu;
                if (romBankLow_ == 0u) romBankLow_ = 1u;
                updateMbc1Banking();
                result.romBankChanged = true;
            }
            ramEnabled_ = ((value & 0x0Au) == 0x0Au);
            result.handled = true;
        }
        break;

    case CartridgeMapper::MBC3:
        if (address < 0x2000u) {
            ramEnabled_ = ((value & 0x0Fu) == 0x0Au);
            result.handled = true;
        } else if (address < 0x4000u) {
            romBankLow_ = value & 0x7Fu;
            if (romBankLow_ == 0u) romBankLow_ = 1u;
            updateMbc1Banking();
            result.romBankChanged = true;
            result.handled = true;
        } else if (address < 0x6000u) {
            ramBankSelect_ = value;
            result.handled = true;
        } else if (address < 0x8000u) {
            // Latch clock (write 0x00 to latch RTC time)
            result.handled = true;
        }
        break;

    case CartridgeMapper::MBC5:
        if (address < 0x2000u) {
            ramEnabled_ = ((value & 0x0Fu) == 0x0Au);
            result.handled = true;
        } else if (address < 0x4000u) {
            // ROM bank low byte (8 bits)
            romBankLow_ = value;
            updateMbc1Banking();
            result.romBankChanged = true;
            result.handled = true;
        } else if (address < 0x6000u) {
            // RAM bank select (MBC5 supports up to 4 RAM banks)
            ramBankSelect_ = value & 0x0Fu;
            result.handled = true;
        } else if (address < 0x8000u) {
            result.handled = true;
        }
        break;

    default:
        break;
    }

    return result;
}

void GameBoyMapper::updateMbc1Banking() {
    // In MBC1, the effective ROM bank number is constructed from:
    // - Low 5 bits from $4000-$5FFF write
    // - High 2 bits from $6000-$7FFF write (when in bank mode 1)
    // Bit 5 of the result is always set to 1
    uint8_t bank = romBankLow_;
    bank |= (ramBankSelect_ & 0x03u) << 5u;
    // Clamp to valid range
    if (bank >= static_cast<uint8_t>(romBankCount_)) {
        bank = static_cast<uint8_t>(romBankCount_ - 1u);
    }
    effectiveRomBank_ = bank;
}

bool GameBoyMapper::isRamAddress(uint16_t addr) const noexcept {
    return mapperType_ != CartridgeMapper::None &&
           ramEnabled_ && addr >= 0xA000u && addr < 0xC000u;
}

bool GameBoyMapper::ramRead(uint16_t addr, std::span<uint8_t> out) const {
    if (!isRamAddress(addr)) return false;
    if (externalRam_.empty()) return false;

    std::size_t offset = addr - 0xA000u;
    std::size_t count = std::min<std::size_t>(out.size(), externalRamSize_ - offset);
    if (count == 0) return false;

    std::memcpy(out.data(), externalRam_.data() + offset, count);
    return true;
}

bool GameBoyMapper::ramWrite(uint16_t addr, std::span<const uint8_t> value) {
    if (!isRamAddress(addr)) return false;
    if (externalRam_.empty()) return false;

    std::size_t offset = addr - 0xA000u;
    std::size_t count = std::min<std::size_t>(value.size(), externalRamSize_ - offset);
    if (count == 0) return false;

    std::memcpy(externalRam_.data() + offset, value.data(), count);
    dirty_ = true;
    return true;
}

void GameBoyMapper::copyRomBankWindow(std::size_t bankIndex, std::span<uint8_t> window) const {
    if (bankIndex >= romBankCount_) {
        std::fill(window.begin(), window.end(), 0xFF);
        return;
    }
    std::size_t start = bankIndex * 0x4000u;
    std::size_t count = std::min<std::size_t>(window.size(), romData_.size() - start);
    std::memcpy(window.data(), romData_.data() + start, count);
    // Fill remaining with 0xFF
    if (count < window.size()) {
        std::fill(window.begin() + count, window.end(), 0xFF);
    }
}

GameBoyMapper::SaveSnapshot GameBoyMapper::extractDirtySaveSnapshot() const {
    SaveSnapshot snapshot;
    if (!dirty_ || externalRam_.empty()) return snapshot;
    snapshot.externalRam = externalRam_;
    return snapshot;
}

void GameBoyMapper::flushSnapshot(const SaveSnapshot& snapshot) {
    // No-op; caller handles file I/O
    (void)snapshot;
}

} // namespace GB
