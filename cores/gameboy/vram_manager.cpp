#include "vram_manager.hpp"
#include <iostream>

VramManager::VramManager() : current_bank(0), sprite_context(0), vram_ptr(nullptr) {}

void VramManager::update_bank(uint16_t bank) {
    current_bank = bank;
}

void VramManager::set_sprite_context(uint8_t context) {
    sprite_context = context;
}

void VramManager::set_vram_source(uint8_t* ptr) {
    vram_ptr = ptr;
}

uint8_t VramManager::read_memory(uint16_t addr) {
    if (!vram_ptr) {
        return 0xFFu;
    }

    // Sprite rendering can require data from the alternate VRAM bank.
    // If sprite_context is non-zero, we force bank 1.
    // If sprite_context is zero, we respect the current_bank state.
    if (sprite_context != 0) {
        if (current_bank != 1) {
            current_bank = 1;
        }
    }

    // Bank 0 is 0x8000-0x8FFF, Bank 1 is 0x9000-0x9FFF.
    // The addr passed is the 4KB offset within the active bank.
    // Since vram_ptr points to the start of the 8KB VRAM (0x8000),
    // Bank 0 is at offset 0x0000, Bank 1 is at offset 0x1000.
    uint16_t bank_offset = (current_bank == 1) ? 0x1000u : 0x0000u;
    
    return vram_ptr[bank_offset + (addr & 0x0FFFu)];
}

uint16_t VramManager::get_current_bank() const {
    return current_bank;
}
