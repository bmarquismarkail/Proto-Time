#include "GameGearCartridge.hpp"

GameGearCartridge::GameGearCartridge() {}
GameGearCartridge::~GameGearCartridge() {}

bool GameGearCartridge::load(const uint8_t* data, size_t size) {
    if (data == nullptr && size > 0u) {
        return false;
    }

    if (size == 0u) {
        rom.clear();
        sram.clear();
        reset();
        return true;
    }

    rom.assign(data, data + size);
    sram.assign(kSramSize, 0u);
    reset();
    return true;
}

void GameGearCartridge::reset() {
    const auto banks = numBanks();
    bankRegisters_[0] = static_cast<uint8_t>(0u % banks);
    bankRegisters_[1] = static_cast<uint8_t>(1u % banks);
    bankRegisters_[2] = static_cast<uint8_t>(2u % banks);
    controlRegister_ = 0u;
}

bool GameGearCartridge::loaded() const noexcept {
    return !rom.empty();
}

bool GameGearCartridge::handlesControlWrite(uint16_t addr) const noexcept {
    return addr >= 0xFFFCu;
}

bool GameGearCartridge::handlesMappedWrite(uint16_t addr) const noexcept {
    return addr >= 0x8000u && addr < 0xC000u && sramEnabled();
}

uint8_t GameGearCartridge::read(uint16_t addr) const {
    if (!loaded() || addr >= 0xC000u) {
        return 0xFFu;
    }

    if (addr >= 0x8000u && sramEnabled()) {
        const auto offset = sramOffset(addr);
        return offset < sram.size() ? sram[offset] : 0xFFu;
    }

    const std::size_t pageIndex = static_cast<std::size_t>(addr / kPageSize);
    const std::size_t bankNum = pageBank(pageIndex);
    const std::size_t romOffset = bankNum * kPageSize + static_cast<std::size_t>(addr % kPageSize);
    return romOffset < rom.size() ? rom[romOffset] : 0xFFu;
}

void GameGearCartridge::write(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0xFFFCu:
        case 0xFFFDu:
        case 0xFFFEu:
            bankRegisters_[static_cast<std::size_t>(addr - 0xFFFCu)] = value;
            return;
        case 0xFFFFu:
            controlRegister_ = value;
            return;
        default:
            break;
    }

    if (addr >= 0x8000u && addr < 0xC000u && sramEnabled()) {
        const auto offset = sramOffset(addr);
        if (offset < sram.size()) {
            sram[offset] = value;
        }
    }
}

std::size_t GameGearCartridge::numBanks() const noexcept {
    return rom.empty() ? 1u : ((rom.size() + (kPageSize - 1u)) / kPageSize);
}

std::size_t GameGearCartridge::pageBank(std::size_t pageIndex) const noexcept {
    const auto banks = numBanks();
    if (pageIndex >= bankRegisters_.size()) {
        return 0u;
    }
    return static_cast<std::size_t>(bankRegisters_[pageIndex]) % banks;
}

bool GameGearCartridge::sramEnabled() const noexcept {
    return (controlRegister_ & 0x01u) != 0u;
}

std::size_t GameGearCartridge::sramOffset(uint16_t addr) const noexcept {
    const std::size_t bankOffset = (controlRegister_ & 0x02u) != 0u ? kSramWindowSize : 0u;
    return bankOffset + static_cast<std::size_t>(addr - 0x8000u);
}
