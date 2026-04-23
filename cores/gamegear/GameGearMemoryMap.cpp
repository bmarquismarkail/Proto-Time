#include "GameGearMemoryMap.hpp"
#include "GameGearInput.hpp"
#include "GameGearVDP.hpp"

void GameGearMemoryMap::setInput(GameGearInput* inputPtr) {
    input = inputPtr;
}

void GameGearMemoryMap::setVdp(GameGearVDP* vdpPtr) {
    vdp = vdpPtr;
}

uint8_t GameGearMemoryMap::readIoPort(uint8_t port) {
    if (port == 0xBEu) {
        return vdp ? vdp->readDataPort() : 0xFFu;
    }
    if (port == 0xBFu) {
        return vdp ? vdp->readControlPort() : 0xFFu;
    }
    if (port == 0xDCu) {
        return input ? input->readInputs() : 0xFFu;
    }
    return 0xFFu;
}

void GameGearMemoryMap::writeIoPort(uint8_t port, uint8_t value) {
    if (port == 0xBEu) {
        if (vdp) {
            vdp->writeDataPort(value);
        }
        return;
    }
    if (port == 0xBFu) {
        if (vdp) {
            vdp->writeControlPort(value);
        }
        return;
    }
}


GameGearMemoryMap::GameGearMemoryMap() {}
GameGearMemoryMap::~GameGearMemoryMap() {}

void GameGearMemoryMap::reset() {
    ram.fill(0);
}

void GameGearMemoryMap::mapRom(const uint8_t* data, size_t size) {
    rom.assign(data, data + size);
    // Compute number of 16KiB banks
    if (size == 0u) {
        romNumBanks_ = 0u;
    } else {
        romNumBanks_ = (size + 0x3FFFu) / 0x4000u;
    }
    // Default bank registers map naturally (0,1,2) modulo available banks
    const std::size_t numBanks = romNumBanks_ > 0u ? romNumBanks_ : 1u;
    bankRegisters_[0] = static_cast<uint8_t>(0u % numBanks);
    bankRegisters_[1] = static_cast<uint8_t>(1u % numBanks);
    bankRegisters_[2] = static_cast<uint8_t>(2u % numBanks);
}

void GameGearMemoryMap::clearRom() {
    rom.clear();
    romNumBanks_ = 0u;
    bankRegisters_[0] = bankRegisters_[1] = bankRegisters_[2] = 0u;
}

uint8_t GameGearMemoryMap::read(uint16_t addr) const {
    if (addr >= 0x8000u && addr < 0xA000u && vdp != nullptr) {
        return vdp->readVram(addr);
    }
    if (addr >= 0xFE00u && addr < 0xFEA0u && vdp != nullptr) {
        return vdp->readOam(addr);
    }
    if (addr >= 0xFF40u && addr <= 0xFF4Bu && vdp != nullptr) {
        return vdp->readRegister(addr);
    }
    if (addr == 0xFF00u) {
        return input ? input->readInputs() : 0xFFu;
    }
    if (addr == 0x00DC) {
        return input ? input->readInputs() : 0xFF;
    }
    if ((addr & 0xFF00) == 0x7F00) {
        return 0x00;
    }
    // 0x0000-0xBFFF: ROM (up to 48KB window, banked)
    if (addr < 0xC000) {
        if (rom.empty()) {
            return 0xFFu;
        }
        // Map into 16KiB pages using bank registers. Each bank register
        // selects a 16KiB page (wrapping by number of available banks).
        const std::size_t pageSize = 0x4000u;
        const std::size_t pageIndex = static_cast<std::size_t>(addr / pageSize);
        const std::size_t bankRegIndex = pageIndex < bankRegisters_.size() ? pageIndex : 0u;
        const std::size_t numBanks = romNumBanks_ > 0u ? romNumBanks_ : 1u;
        const std::size_t bankNum = static_cast<std::size_t>(bankRegisters_[bankRegIndex]) % numBanks;
        const std::size_t offsetInBank = static_cast<std::size_t>(addr % pageSize);
        const std::size_t romOffset = bankNum * pageSize + offsetInBank;
        if (romOffset < rom.size()) {
            return rom[romOffset];
        }
        return 0xFFu;
    }
    // 0xC000-0xDFFF: RAM (8KB)
    if (addr >= 0xC000 && addr < 0xE000) {
        return ram[addr - 0xC000];
    }
    // 0xE000-0xFFFF: RAM mirror
    if (addr >= 0xE000) {
        return ram[addr - 0xE000];
    }
    return 0xFF;
}

void GameGearMemoryMap::write(uint16_t addr, uint8_t value) {
    // Bank register writes: address range 0xFFFC..0xFFFE control the 3x16KiB
    // bank registers used to map 0x0000..0xBFFF. This keeps behavior local
    // to the Game Gear memory map (simple mapper support).
    if (addr >= 0xFFFCu && addr <= 0xFFFEu) {
        const std::size_t idx = static_cast<std::size_t>(addr - 0xFFFCu);
        if (idx < bankRegisters_.size()) {
            bankRegisters_[idx] = value;
        }
        return;
    }
    if (addr >= 0x8000u && addr < 0xA000u && vdp != nullptr) {
        vdp->writeVram(addr, value);
        return;
    }
    if (addr >= 0xFE00u && addr < 0xFEA0u && vdp != nullptr) {
        vdp->writeOam(addr, value);
        return;
    }
    if (addr >= 0xFF40u && addr <= 0xFF4Bu && vdp != nullptr) {
        vdp->writeRegister(addr, value);
        return;
    }
    // 0xC000-0xDFFF: RAM (8KB)
    if (addr >= 0xC000 && addr < 0xE000) {
        ram[addr - 0xC000] = value;
        return;
    }
    // 0xE000-0xFFFF: RAM mirror
    if (addr >= 0xE000) {
        ram[addr - 0xE000] = value;
        return;
    }
    if ((addr & 0xFF00) == 0x7F00) {
        return;
    }
}
