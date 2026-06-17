#pragma once
#include <cstdint>

using AddressType = uint16_t;
using DataType = uint8_t;

class VramManager {
public:
    VramManager();
    void update_bank(uint16_t bank);
    void set_sprite_context(uint8_t context);
    uint8_t read_memory(uint16_t addr);
    uint16_t get_current_bank() const;

private:
    uint16_t current_bank = 0;
    uint8_t sprite_context = 0;
};
