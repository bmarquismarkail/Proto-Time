#include "cores/gamegear/GameGearCartridge.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    GameGearCartridge cartridge;
    assert(!cartridge.load(nullptr, 4u));

    const uint8_t rom[] = {0x12u, 0x34u, 0x56u, 0x78u};
    assert(cartridge.load(rom, sizeof(rom)));
    assert(cartridge.loaded());

    const std::size_t page = 0x4000u;
    std::vector<uint8_t> bankedRom(page * 4u);
    for (std::size_t i = 0; i < page; ++i) bankedRom[i] = 0x00u;
    for (std::size_t i = 0; i < page; ++i) bankedRom[page + i] = 0x11u;
    for (std::size_t i = 0; i < page; ++i) bankedRom[page * 2u + i] = 0x22u;
    for (std::size_t i = 0; i < page; ++i) bankedRom[page * 3u + i] = 0x33u;

    assert(cartridge.load(bankedRom.data(), bankedRom.size()));
    assert(cartridge.read(0x0000u) == 0x00u);
    assert(cartridge.read(0x4000u) == 0x11u);
    assert(cartridge.read(0x8000u) == 0x22u);

    cartridge.write(0xFFFDu, 0x03u);
    assert(cartridge.read(0x4000u) == 0x33u);

    cartridge.write(0xFFFFu, 0x01u);
    cartridge.write(0x8000u, 0xA5u);
    cartridge.write(0xBFFFu, 0x5Au);
    assert(cartridge.read(0x8000u) == 0xA5u);
    assert(cartridge.read(0xBFFFu) == 0x5Au);

    cartridge.write(0xFFFFu, 0x03u);
    assert(cartridge.read(0x8000u) == 0x00u);
    cartridge.write(0x8000u, 0x3Cu);
    assert(cartridge.read(0x8000u) == 0x3Cu);

    cartridge.write(0xFFFFu, 0x01u);
    assert(cartridge.read(0x8000u) == 0xA5u);

    assert(cartridge.load(nullptr, 0u));
    assert(!cartridge.loaded());
    return 0;
}
