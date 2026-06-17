#include "vram_manager.hpp"
#include <iostream>

VramManager::VramManager() : current_bank(0), sprite_context(0) {}

void VramManager::update_bank(uint16_t bank) {
    current_bank = bank;
}

void VramManager::set_sprite_context(uint8_t context) {
    sprite_context = context;
}

uint8_t VramManager::read_memory(uint16_t addr) {
    (void)addr;

    // Sprite rendering can require data from the alternate VRAM bank.
    uint16_t target_bank = sprite_context != 0 ? 1 : 0;

    if (current_bank != target_bank) {
        current_bank = target_bank;
    }

    return 0xAA; // Return dummy data
}

uint16_t VramManager::get_current_bank() const {
    return current_bank;
}
