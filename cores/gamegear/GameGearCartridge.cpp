#include "GameGearCartridge.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

void appendU32(std::vector<uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
}

std::uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t& pos)
{
    if (pos > bytes.size() || bytes.size() - pos < 4u) {
        throw std::invalid_argument("Game Gear cartridge state truncated");
    }
    const auto value = static_cast<std::uint32_t>(bytes[pos]) |
        (static_cast<std::uint32_t>(bytes[pos + 1u]) << 8u) |
        (static_cast<std::uint32_t>(bytes[pos + 2u]) << 16u) |
        (static_cast<std::uint32_t>(bytes[pos + 3u]) << 24u);
    pos += 4u;
    return value;
}

std::vector<uint8_t> readBytes(const std::vector<uint8_t>& bytes, std::size_t& pos, std::size_t maxSize)
{
    const auto size = static_cast<std::size_t>(readU32(bytes, pos));
    if (size > maxSize || pos > bytes.size() || bytes.size() - pos < size) {
        throw std::invalid_argument("Game Gear cartridge state vector invalid");
    }
    std::vector<uint8_t> out(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                             bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
    pos += size;
    return out;
}

}

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
    saveDirty_ = false;
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

    if (addr < 0x0400u) {
        return rom[addr % rom.size()];
    }

    const std::size_t pageIndex = static_cast<std::size_t>(addr / kPageSize);
    const std::size_t bankNum = pageBank(pageIndex);
    const std::size_t romOffset = bankNum * kPageSize + static_cast<std::size_t>(addr % kPageSize);
    return romOffset < rom.size() ? rom[romOffset] : 0xFFu;
}

void GameGearCartridge::write(uint16_t addr, uint8_t value) {
    // Mapper registers $FFFC-$FFFF
    if (addr >= 0xFFFCu) {
        if (addr == 0xFFFCu) {
            controlRegister_ = value;
        } else {
            bankRegisters_[static_cast<std::size_t>(addr - 0xFFFDu)] = value;
        }
        // RAM mirror update is handled by GameGearMemoryMap
        return;
    }
    if (addr >= 0x8000u && addr < 0xC000u && sramEnabled()) {
        const auto offset = sramOffset(addr);
        if (offset < sram.size()) {
            if (sram[offset] != value) {
                sram[offset] = value;
                saveDirty_ = true;
            }
        }
    }
}

bool GameGearCartridge::supportsSaveData() const noexcept {
    return !sram.empty();
}

bool GameGearCartridge::hasDirtySaveData() const noexcept {
    return saveDirty_;
}

void GameGearCartridge::markSaveClean() noexcept {
    saveDirty_ = false;
}

std::vector<uint8_t> GameGearCartridge::exportSaveData() const {
    return sram;
}

void GameGearCartridge::importSaveData(const std::vector<uint8_t>& saveData) {
    if (sram.empty()) {
        saveDirty_ = false;
        return;
    }
    std::fill(sram.begin(), sram.end(), 0u);
    const auto count = std::min(sram.size(), saveData.size());
    std::copy_n(saveData.begin(), count, sram.begin());
    saveDirty_ = false;
}

std::vector<uint8_t> GameGearCartridge::exportState() const {
    std::vector<uint8_t> state;
    appendU32(state, static_cast<std::uint32_t>(rom.size()));
    appendU32(state, static_cast<std::uint32_t>(numBanks()));
    state.push_back(controlRegister_);
    state.insert(state.end(), bankRegisters_.begin(), bankRegisters_.end());
    state.push_back(saveDirty_ ? 1u : 0u);
    appendU32(state, static_cast<std::uint32_t>(sram.size()));
    state.insert(state.end(), sram.begin(), sram.end());
    return state;
}

void GameGearCartridge::importState(const std::vector<uint8_t>& state) {
    std::size_t pos = 0;
    const auto romSize = static_cast<std::size_t>(readU32(state, pos));
    const auto bankCount = static_cast<std::size_t>(readU32(state, pos));
    if (romSize != rom.size() || bankCount != numBanks()) {
        throw std::invalid_argument("Game Gear cartridge state does not match loaded ROM");
    }
    if (pos > state.size() || state.size() - pos < 5u) {
        throw std::invalid_argument("Game Gear cartridge state truncated");
    }
    const auto control = state[pos++];
    std::array<uint8_t, 3> banks{};
    std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(pos), banks.size(), banks.begin());
    pos += banks.size();
    const auto dirty = state[pos++];
    if (dirty > 1u) {
        throw std::invalid_argument("Game Gear cartridge state dirty flag invalid");
    }
    auto nextSram = readBytes(state, pos, kSramSize);
    if (nextSram.size() != sram.size() || pos != state.size()) {
        throw std::invalid_argument("Game Gear cartridge state SRAM size mismatch");
    }

    controlRegister_ = control;
    bankRegisters_ = banks;
    saveDirty_ = dirty != 0u;
    sram = std::move(nextSram);
}

std::size_t GameGearCartridge::numBanks() const noexcept {
    return rom.empty() ? 1u : ((rom.size() + (kPageSize - 1u)) / kPageSize);
}

std::size_t GameGearCartridge::pageBank(std::size_t pageIndex) const noexcept {
    const auto banks = numBanks();
    if (pageIndex >= bankRegisters_.size()) {
        return 0u;
    }
    const auto bankShift = static_cast<std::size_t>(controlRegister_ & 0x03u) * 8u;
    return (static_cast<std::size_t>(bankRegisters_[pageIndex]) + bankShift) % banks;
}

bool GameGearCartridge::sramEnabled() const noexcept {
    return (controlRegister_ & 0x08u) != 0u;
}

std::size_t GameGearCartridge::sramOffset(uint16_t addr) const noexcept {
    const std::size_t bankOffset = (controlRegister_ & 0x04u) != 0u ? kSramWindowSize : 0u;
    return bankOffset + static_cast<std::size_t>(addr - 0x8000u);
}
