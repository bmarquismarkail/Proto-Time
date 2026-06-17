#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "cores/gameboy/vram_manager.hpp"

namespace {

constexpr uint16_t kBankA = 0;
constexpr uint16_t kBankB = 1;
constexpr uint8_t kSpriteContextActive = 1;
constexpr uint8_t kExpectedBankBData = 0xAA;

void test_auto_bank_switch_on_sprite_access()
{
    VramManager vramManager;
    vramManager.update_bank(kBankA);
    vramManager.set_sprite_context(kSpriteContextActive);

    const auto data = vramManager.read_memory(0xA100u);

    if (vramManager.get_current_bank() != kBankB) {
        throw std::runtime_error("VRAM manager did not switch to bank B for sprite access");
    }
    if (data != kExpectedBankBData) {
        throw std::runtime_error("VramManager returned unexpected bank B data");
    }
}

} // namespace

int main()
{
    std::cout << "Running VRAM banking tests..." << std::endl;
    test_auto_bank_switch_on_sprite_access();
    std::cout << "All VRAM banking tests passed!" << std::endl;
    return 0;
}
