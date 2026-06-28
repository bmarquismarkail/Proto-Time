#include "GameBoyMemoryMap.hpp"
#include "GameBoyMapper.hpp"
#include "cartridge/GameBoyCartridge.hpp"
#include "../../machine/SaveState.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <tuple>

namespace GB {

GameBoyMemoryMap::GameBoyMemoryMap() {
    // Initialize OAM with 0xFF (Pan Docs: undefined state)
    oam_.fill(0xFF);
    ioRegs_.fill(0xFF);
    hram_.fill(0xFF);

    // Register memory regions with the MemoryStorage base class
    // so that CPU memory accesses via attachMemory work correctly.
    addMemBlock(std::make_tuple(uint16_t(0x0000), uint16_t(0x4000), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0x4000), uint16_t(0x4000), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0x8000), uint16_t(0x2000), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xA000), uint16_t(0x2000), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xC000), uint16_t(0x2000), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xE000), uint16_t(0x1E00), BMMQ::memAccess::Unmapped));
    addMemBlock(std::make_tuple(uint16_t(0xFE00), uint16_t(0x00A0), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xFEA0), uint16_t(0x0060), BMMQ::memAccess::Unmapped));
    addMemBlock(std::make_tuple(uint16_t(0xFF00), uint16_t(0x0080), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xFF80), uint16_t(0x007F), BMMQ::memAccess::ReadWrite));
    addMemBlock(std::make_tuple(uint16_t(0xFFFF), uint16_t(0x0001), BMMQ::memAccess::ReadWrite));
    setReadInterceptor([this](uint16_t address, std::span<uint8_t> stream) {
        read(stream, address);
        return true;
    });
    setWriteInterceptor([this](uint16_t address, std::span<const uint8_t> value) {
        write(value, address);
        return true;
    });
}

void GameBoyMemoryMap::reset() {
    vram_.fill(0x00);
    wram_.fill(0xFF);
    oam_.fill(0xFF);
    ioRegs_.fill(0xFF);
    hram_.fill(0xFF);
    bootRomActive_ = false;
    clearBootRom();
    clearRomWindow(0x0000);
    clearRomWindow(0x4000);
}

// Override MemoryStorage::read to use our custom storage arrays
void GameBoyMemoryMap::read(std::span<uint8_t> stream, uint16_t address) const {
    for (std::size_t i = 0; i < stream.size(); ++i) {
        stream[i] = GameBoyMemoryMap::read(static_cast<uint16_t>(address + i));
    }
}

// Override MemoryStorage::write to use our custom storage arrays
void GameBoyMemoryMap::write(std::span<const uint8_t> value, uint16_t address) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        GameBoyMemoryMap::write(static_cast<uint16_t>(address + i), value[i]);
    }
}

uint16_t GameBoyMemoryMap::resolveEchoAddress(uint16_t address) noexcept {
    if (address >= 0xE000 && address <= 0xFDFF) {
        return address - 0x2000;
    }
    return address;
}

uint8_t GameBoyMemoryMap::readRaw(uint16_t addr) const {
    // Boot ROM overlay (0x0000-0x00FF)
    if (bootRomActive_ && addr < bootRom_.size()) {
        return bootRom_[addr];
    }

    // ROM window at 0x0000-0x3FFF
    if (addr < 0x4000u && romWindow0x0000_.active) {
        if (addr < romWindow0x0000_.data.size()) {
            return romWindow0x0000_.data[addr];
        }
        return 0xFF;
    }

    // ROM window at 0x4000-0x7FFF
    if (addr >= 0x4000u && addr < 0x8000u && romWindow0x4000_.active) {
        const auto offset = static_cast<std::size_t>(addr - 0x4000u);
        if (offset < romWindow0x4000_.data.size()) {
            return romWindow0x4000_.data[offset];
        }
        return 0xFF;
    }

    // VRAM (0x8000-0x9FFF)
    if (addr >= 0x8000u && addr < 0xA000u) {
        return vram_[addr - 0x8000u];
    }

    // External RAM (0xA000-0xBFFF)
    if (addr >= 0xA000u && addr < 0xC000u) {
        if (cartridge_ != nullptr) {
            return cartridge_->read(addr);
        }
        uint8_t result = 0xFFu;
        if (handleCartridgeRamRead(addr, {&result, 1})) {
            return result;
        }
        return 0xFF;
    }

    // WRAM (0xC000-0xDFFF)
    if (addr >= 0xC000u && addr < 0xE000u) {
        return wram_[addr - 0xC000u];
    }

    // Echo RAM (0xE000-0xFDFF) — mirror of WRAM (C000-DDFF)
    if (addr >= 0xE000u && addr <= 0xFDFF) {
        uint16_t echoOffset = static_cast<uint16_t>(addr - 0xE000u);
        if (echoOffset < 0x2000u) {
            return wram_[echoOffset];
        }
    }

    // OAM (0xFE00-0xFE9F)
    if (addr >= 0xFE00u && addr < 0xFEA0u) {
        return oam_[addr - 0xFE00u];
    }

    // Unusable (0xFEA0-0xFEFF) — return FF
    if (addr >= 0xFEA0u && addr < 0xFF00u) {
        return 0xFF;
    }

    // I/O registers (0xFF00-0xFF7F)
    if (addr >= 0xFF00u && addr < 0xFF80u) {
        return ioRegs_[addr - 0xFF00u];
    }

    // HRAM (0xFF80-0xFFFE)
    if (addr >= 0xFF80u && addr < 0xFFFFu) {
        return hram_[addr - 0xFF80u];
    }

    // IE (0xFFFF)
    if (addr == 0xFFFFu) {
        return hram_[0xFFu];
    }

    return 0xFF;
}

bool GameBoyMemoryMap::handleBootRomRead(uint16_t addr, std::span<uint8_t> out) const {
    if (!bootRomActive_) return false;
    if (addr >= bootRom_.size()) return false;
    std::size_t count = std::min<std::size_t>(out.size(), bootRom_.size() - addr);
    std::memcpy(out.data(), bootRom_.data() + addr, count);
    return true;
}

bool GameBoyMemoryMap::handleCartridgeRamRead(uint16_t addr, std::span<uint8_t> out) const {
    if (!mapper_) return false;
    return mapper_->ramRead(addr, out);
}

bool GameBoyMemoryMap::handleCartridgeRamWrite(uint16_t addr, std::span<const uint8_t> value) {
    if (cartridge_ != nullptr && !value.empty()) {
        cartridge_->write(addr, value[0]);
        return true;
    }
    if (!mapper_) return false;
    return mapper_->ramWrite(addr, value);
}

void GameBoyMemoryMap::writeRaw(uint16_t addr, uint8_t value) {
    // Boot ROM is writable only at FF50 (disable bit)
    if (addr == 0xFF50) {
        // Handled in handleSpecialWrite
        return;
    }

    // ROM windows are read-only
    if (addr < 0x4000u && romWindow0x0000_.active) {
        return;
    }
    if (addr >= 0x4000u && addr < 0x8000u && romWindow0x4000_.active) {
        return;
    }

    // VRAM (0x8000-0x9FFF)
    if (addr >= 0x8000u && addr < 0xA000u) {
        vram_[addr - 0x8000u] = value;
        return;
    }

    // External RAM (0xA000-0xBFFF) — handled by mapper
    if (addr >= 0xA000u && addr < 0xC000u) {
        return; // Will be caught by handleSpecialWrite
    }

    // WRAM (0xC000-0xDFFF)
    if (addr >= 0xC000u && addr < 0xE000u) {
        wram_[addr - 0xC000u] = value;
        return;
    }

    // Echo RAM (0xE000-0xFDFF) — mirror of WRAM
    if (addr >= 0xE000u && addr <= 0xFDFF) {
        uint16_t echoOffset = static_cast<uint16_t>(addr - 0xE000u);
        if (echoOffset < 0x2000u) {
            wram_[echoOffset] = value;
        }
        return;
    }

    // OAM (0xFE00-0xFE9F)
    if (addr >= 0xFE00u && addr < 0xFEA0u) {
        oam_[addr - 0xFE00u] = value;
        return;
    }

    // Unusable (0xFEA0-0xFEFF) — ignore
    if (addr >= 0xFEA0u && addr < 0xFF00u) {
        return;
    }

    // IE (0xFFFF)
    if (addr == 0xFFFFu) {
        hram_[0xFFu] = value;
        return;
    }

    // I/O registers (0xFF00-0xFF7F)
    if (addr >= 0xFF00u && addr < 0xFF80u) {
        if (addr == 0xFF46u && value != 0xFFu) {
            ioRegs_[addr - 0xFF00u] = value;
            performOamDma(value);
            return;
        }
        if (addr == 0xFF41u) {
            ioRegs_[addr - 0xFF00u] = static_cast<uint8_t>((ioRegs_[addr - 0xFF00u] & 0x07u) |
                                                           (value & 0x78u));
            return;
        }
        if (addr == 0xFF44u) {
            ioRegs_[addr - 0xFF00u] = 0x00u;
            return;
        }
        ioRegs_[addr - 0xFF00u] = value;
        return;
    }

    // HRAM (0xFF80-0xFFFE)
    if (addr >= 0xFF80u && addr < 0xFFFFu) {
        hram_[addr - 0xFF80u] = value;
        return;
    }

}

void GameBoyMemoryMap::performOamDma(uint8_t sourceHighByte) {
    const auto sourceBase = static_cast<uint16_t>(static_cast<uint16_t>(sourceHighByte) << 8u);
    for (std::size_t i = 0; i < oam_.size(); ++i) {
        oam_[i] = readRaw(static_cast<uint16_t>(sourceBase + i));
    }
}

bool GameBoyMemoryMap::handleSpecialWrite(uint16_t addr, std::span<const uint8_t> value) {
    if (value.empty()) return false;

    if (addr < 0x8000u && mapper_ != nullptr) {
        const auto result = mapper_->write(addr, value[0]);
        if (result.romBankChanged) {
            std::array<uint8_t, 0x4000> window{};
            mapper_->copyRomBankWindow(mapper_->currentRomBank(), window);
            installRomWindow(0x4000u, window);
        }
        return result.handled;
    }

    // FF50: Boot ROM disable
    if (addr == 0xFF50 && value[0] != 0) {
        bootRomActive_ = false;
        setIoRegisterRaw(0xFF50u, value[0]);
        return true;
    }

    // External RAM write
    if (addr >= 0xA000u && addr < 0xC000u) {
        return handleCartridgeRamWrite(addr, value);
    }

    return false;
}

uint8_t GameBoyMemoryMap::read(uint16_t addr) const {
    // Resolve echo address for reads
    uint16_t resolved = resolveEchoAddress(addr);

    // Check boot ROM first (only at 0x0000-0x00FF)
    if (bootRomActive_ && addr < 0x100u) {
        uint8_t result;
        if (handleBootRomRead(addr, {&result, 1})) {
            return result;
        }
    }

    // For echo-resolved addresses, use raw read
    return readRaw(resolved);
}

void GameBoyMemoryMap::write(uint16_t addr, uint8_t value) {
    // Resolve echo address for writes
    uint16_t resolved = resolveEchoAddress(addr);

    // Handle special writes (cartridge RAM, FF50)
    if (handleSpecialWrite(resolved, std::span<const uint8_t>(&value, 1))) {
        if (writeObserver_) {
            writeObserver_(resolved, value);
        }
        return;
    }

    writeRaw(resolved, value);
    if (writeObserver_) {
        writeObserver_(resolved, value);
    }
}

void GameBoyMemoryMap::mapBootRom(const uint8_t* data, std::size_t size) {
    if (size > 0x100u) size = 0x100u;
    bootRom_.assign(data, data + size);
    bootRomActive_ = true;
}

void GameBoyMemoryMap::clearBootRom() {
    bootRom_.clear();
    bootRomActive_ = false;
}

void GameBoyMemoryMap::installRomWindow(uint16_t base, std::span<const uint8_t> data) {
    if (base == 0x0000u) {
        romWindow0x0000_.data.assign(data.begin(), data.end());
        romWindow0x0000_.active = true;
    } else if (base == 0x4000u) {
        romWindow0x4000_.data.assign(data.begin(), data.end());
        romWindow0x4000_.active = true;
    }
}

void GameBoyMemoryMap::clearRomWindow(uint16_t base) {
    if (base == 0x0000u) {
        romWindow0x0000_.active = false;
        romWindow0x0000_.data.clear();
    } else if (base == 0x4000u) {
        romWindow0x4000_.active = false;
        romWindow0x4000_.data.clear();
    }
}

void GameBoyMemoryMap::setIoRegisterRaw(uint16_t address, uint8_t value) {
    if (address >= 0xFF00u && address < 0xFF80u) {
        ioRegs_[address - 0xFF00u] = value;
    } else if (address == 0xFFFFu) {
        hram_[0xFFu] = value;
    }
}



std::vector<uint8_t> GameBoyMemoryMap::exportState() const {
    std::vector<uint8_t> state;
    const auto appendU32 = [&state](std::uint32_t value) {
        state.push_back(static_cast<uint8_t>(value & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        state.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };
    const auto appendBytes = [&state](const auto& bytes) {
        state.insert(state.end(), bytes.begin(), bytes.end());
    };

    state.reserve(vram_.size() + wram_.size() + oam_.size() + ioRegs_.size() + hram_.size() +
                  bootRom_.size() + romWindow0x0000_.data.size() + romWindow0x4000_.data.size() + 32u);

    state.push_back(bootRomActive_ ? 1u : 0u);
    appendU32(static_cast<std::uint32_t>(bootRom_.size()));
    appendBytes(bootRom_);

    state.push_back(romWindow0x0000_.active ? 1u : 0u);
    appendU32(static_cast<std::uint32_t>(romWindow0x0000_.data.size()));
    appendBytes(romWindow0x0000_.data);

    state.push_back(romWindow0x4000_.active ? 1u : 0u);
    appendU32(static_cast<std::uint32_t>(romWindow0x4000_.data.size()));
    appendBytes(romWindow0x4000_.data);

    appendBytes(vram_);
    appendBytes(wram_);
    appendBytes(oam_);
    appendBytes(ioRegs_);
    appendBytes(hram_);

    uint32_t crc = BMMQ::crc32(state.data(), state.size());
    state.push_back(static_cast<uint8_t>(crc & 0xFF));
    state.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    state.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    state.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));

    return state;
}

void GameBoyMemoryMap::importState(const std::vector<uint8_t>& state) {
    const auto fixedPayloadSize = vram_.size() + wram_.size() + oam_.size() + ioRegs_.size() + hram_.size();
    if (state.size() < 4u + 15u + fixedPayloadSize) {
        throw std::invalid_argument("Memory map state too short");
    }

    const auto crcPos = state.size() - 4u;
    const uint32_t storedCrc =
        static_cast<uint32_t>(state[crcPos]) |
        (static_cast<uint32_t>(state[crcPos + 1u]) << 8u) |
        (static_cast<uint32_t>(state[crcPos + 2u]) << 16u) |
        (static_cast<uint32_t>(state[crcPos + 3u]) << 24u);
    const uint32_t expectedCrc = BMMQ::crc32(state.data(), crcPos);
    if (storedCrc != expectedCrc) {
        throw std::invalid_argument("Memory map state checksum mismatch");
    }

    std::size_t pos = 0;
    const auto payloadEnd = crcPos;
    const auto require = [&](std::size_t count) {
        if (pos > payloadEnd || count > payloadEnd - pos) {
            throw std::invalid_argument("Memory map state is truncated");
        }
    };
    const auto readFlag = [&]() -> bool {
        require(1u);
        const auto value = state[pos++];
        if (value > 1u) {
            throw std::invalid_argument("Memory map state contains invalid flag");
        }
        return value != 0u;
    };
    const auto readU32 = [&]() -> std::uint32_t {
        require(4u);
        const auto value = static_cast<std::uint32_t>(state[pos]) |
            (static_cast<std::uint32_t>(state[pos + 1u]) << 8u) |
            (static_cast<std::uint32_t>(state[pos + 2u]) << 16u) |
            (static_cast<std::uint32_t>(state[pos + 3u]) << 24u);
        pos += 4u;
        return value;
    };
    const auto readVector = [&](std::size_t maxSize) {
        const auto length = readU32();
        if (length > maxSize) {
            throw std::invalid_argument("Memory map state section is too large");
        }
        require(length);
        std::vector<uint8_t> bytes(state.begin() + static_cast<std::ptrdiff_t>(pos),
                                   state.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;
        return bytes;
    };
    const auto readArray = [&](auto& out) {
        require(out.size());
        std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(pos), out.size(), out.begin());
        pos += out.size();
    };

    const bool bootActive = readFlag();
    auto bootRom = readVector(0x100u);
    if (bootActive && bootRom.empty()) {
        throw std::invalid_argument("Active boot ROM state has no boot ROM data");
    }

    const bool win0Active = readFlag();
    auto win0 = readVector(0x4000u);
    const bool win4Active = readFlag();
    auto win4 = readVector(0x4000u);

    decltype(vram_) nextVram{};
    decltype(wram_) nextWram{};
    decltype(oam_) nextOam{};
    decltype(ioRegs_) nextIoRegs{};
    decltype(hram_) nextHram{};
    readArray(nextVram);
    readArray(nextWram);
    readArray(nextOam);
    readArray(nextIoRegs);
    readArray(nextHram);
    if (pos != payloadEnd) {
        throw std::invalid_argument("Memory map state contains trailing payload data");
    }

    bootRom_ = std::move(bootRom);
    bootRomActive_ = bootActive;
    romWindow0x0000_.active = win0Active;
    romWindow0x0000_.data = win0Active ? std::move(win0) : std::vector<uint8_t>{};
    romWindow0x4000_.active = win4Active;
    romWindow0x4000_.data = win4Active ? std::move(win4) : std::vector<uint8_t>{};
    vram_ = nextVram;
    wram_ = nextWram;
    oam_ = nextOam;
    ioRegs_ = nextIoRegs;
    hram_ = nextHram;
}

} // namespace GB
