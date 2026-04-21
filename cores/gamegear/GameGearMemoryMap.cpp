#include "GameGearMemoryMap.hpp"
#include "GameGearInput.hpp"

void GameGearMemoryMap::setInput(GameGearInput* inputPtr) {
    input = inputPtr;
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
    // Input port and I/O stubs must override the broad ROM window.
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
    // VDP/PSG I/O stub (future: real mapping)
    if ((addr & 0xFF00) == 0x7F00) {
        // Ignore writes for now
        return;
    }
    // TODO: Add ROM banking, I/O, and mapper support
}
