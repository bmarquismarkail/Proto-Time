#include <cstdint>
#include <iostream>
#include <stdexcept>

constexpr uint16_t kBankA = 0;
constexpr uint16_t kBankB = 1;
constexpr uint8_t kSpriteContextActive = 1;
constexpr uint8_t kExpectedBankBData = 0xAA;

#include "cores/gameboy/vram_manager.hpp"

void test_auto_bank_switch_on_sprite_access() {
    VramManager vramManager;
    uint8_t* dummyVram = new uint8_t[0x2000]();
    for(uint16_t i=0; i<0x2000; ++i) dummyVram[i] = 0xFF;
    dummyVram[0x1000 + (0xA100u & 0x0FFFu)] = kExpectedBankBData;

    vramManager.set_vram_source(dummyVram);
    vramManager.update_bank(kBankA);
    vramManager.set_sprite_context(kSpriteContextActive);

    std::cout << "Current Bank before read: " << (int)vramManager.get_current_bank() << std::endl;
    const auto data = vramManager.read_memory(0xA100u);
    std::cout << "Current Bank after read: " << (int)vramManager.get_current_bank() << std::endl;
    std::cout << "Data read: " << (int)data << std::endl;

    if (vramManager.get_current_bank() != kBankB) {
        throw std::runtime_error("VRAM manager did not switch to bank B for sprite access");
    }
    if (data != kExpectedBankBData) {
        throw std::runtime_error("VramManager returned unexpected bank B data");
    }

    delete[] dummyVram;
    std::cout << "Test Passed!" << std::endl;
}

int main() {
    std::cout << "Running VRAM banking tests..." << std::endl;
    try {
        test_auto_bank_switch_on_sprite_access();
        std::cout << "All tests passed successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
