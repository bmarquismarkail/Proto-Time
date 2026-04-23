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
}

void GameGearMemoryMap::clearRom() {
    rom.clear();
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
        if (!rom.empty()) {
            size_t offset = addr % rom.size();
            return rom[offset];
        }
        return 0xFF;
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
