#include "GameGearCartridge.hpp"

GameGearCartridge::GameGearCartridge() {}
GameGearCartridge::~GameGearCartridge() {}

bool GameGearCartridge::load(const uint8_t* data, size_t size) {
    if (data == nullptr && size > 0u) {
        return false;
    }

    if (size == 0u) {
        rom.clear();
        return true;
    }

    rom.assign(data, data + size);
    // TODO: Validate ROM, handle mappers
    return true;
}
