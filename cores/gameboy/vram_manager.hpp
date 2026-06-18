#pragma once
#include <cstdint>

using AddressType = uint16_t;
using DataType = uint8_t;

class VramManager {
public:
    VramManager();
    void update_bank(uint16_t bank);
    void set_sprite_context(uint8_t context);
    void set_vram_source(uint8_t* ptr);
    uint8_t read_memory(uint16_t addr);
    uint8_t write_memory(uint16_t addr, uint8_t value);
    uint16_t get_current_bank() const;

private:
    uint16_t current_bank = 0;
    uint8_t sprite_context = 0;
    uint8_t* vram_ptr = nullptr;
};
