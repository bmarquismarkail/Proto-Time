#include "vram_manager.hpp"
#include <iostream>
#include <cstdint>

VramManager::VramManager() : current_bank(0), sprite_context(0), vram_ptr(nullptr) {}

void VramManager::update_bank(uint16_t bank) {
    current_bank = bank;
}

uint8_t VramManager::read_memory(uint16_t address) {
    if (!vram_ptr) {
        return 0xFFu;
    }

    if (sprite_context != 0) {
        if (current_bank != 1) {
            current_bank = 1;
            std::cout << "[VramManager] Switching bank to 1 due to sprite context" << std::endl;
        }
    }

    uint16_t bank_offset = (current_bank == 1) ? 0x1000u : 0x0000u;
    
    return vram_ptr[bank_offset + (address & 0x0FFFu)];
}

uint8_t VramManager::write_memory(uint16_t address, uint8_t value) {
    if (!vram_ptr) {
        return 0xFFu;
    }

    uint16_t bank_offset = (current_bank == 1) ? 0x1000u : 0x0000u;
    vram_ptr[bank_offset + (address & 0x0FFFu)] = value;
    return value;
}

void VramManager::set_vram_source(uint8_t* ptr) {
    vram_ptr = ptr;
}

uint16_t VramManager::get_current_bank() const {
    return current_bank;
}

void VramManager::set_sprite_context(uint8_t context) {
    sprite_context = context;
}
