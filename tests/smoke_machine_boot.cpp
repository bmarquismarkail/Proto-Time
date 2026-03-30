#include <cassert>
#include <cstdint>

#include "cores/gameboy/GameBoyMachine.hpp"

int main() {
    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    machine.loadRom({0x3E, 0x12, 0x00});
    host.step();
    assert(host.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));
    return 0;
}
