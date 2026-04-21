#include "cores/gamegear/GameGearCartridge.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    GameGearCartridge cartridge;
    assert(!cartridge.load(nullptr, 4u));

    const uint8_t rom[] = {0x12u, 0x34u, 0x56u, 0x78u};
    assert(cartridge.load(rom, sizeof(rom)));
    assert(cartridge.load(nullptr, 0u));
    return 0;
}
