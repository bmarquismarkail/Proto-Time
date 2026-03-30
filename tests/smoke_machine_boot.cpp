#include <cassert>
#include <cstdint>

#include "cores/gameboy/GameBoyMachine.hpp"

int main() {
    GameBoyMachine machine;
    machine.loadRom({0x3E, 0x12, 0x00});
    machine.stepBaseline();
    assert(machine.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));
    return 0;
}
