#include "GameGearMemoryMap.hpp"

GameGearMemoryMap::GameGearMemoryMap() {}
GameGearMemoryMap::~GameGearMemoryMap() {}

void GameGearMemoryMap::reset() {
    // TODO: Reset RAM, I/O, etc.
}

uint8_t GameGearMemoryMap::read(uint16_t addr) const {
    // TODO: Implement memory read
    (void)addr;
    return 0xFF;
}

void GameGearMemoryMap::write(uint16_t addr, uint8_t value) {
    // TODO: Implement memory write
    (void)addr;
    (void)value;
}
