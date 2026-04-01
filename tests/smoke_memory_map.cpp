#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "machine/MemoryMap.hpp"

int main()
{
    BMMQ::MemoryMap map;

    bool unmappedReadThrew = false;
    try {
        (void)map.read8(0x0000);
    } catch (const std::out_of_range&) {
        unmappedReadThrew = true;
    }
    assert(unmappedReadThrew);

    map.mapRom(0x0000, 0x4000);
    map.mapRam(0xC000, 0x2000);
    map.installRom(std::vector<uint8_t>{0x3E, 0x12, 0x00}, 0x0000);

    assert(map.read8(0x0000) == 0x3E);
    assert(map.read8(0x0001) == 0x12);
    assert(map.read8(0x0002) == 0x00);

    bool romWriteThrew = false;
    try {
        map.write8(0x0000, 0x99);
    } catch (const std::out_of_range&) {
        romWriteThrew = true;
    }
    assert(romWriteThrew);
    assert(map.read8(0x0000) == 0x3E);

    map.write8(0xC000, 0x77);
    assert(map.read8(0xC000) == 0x77);

    return 0;
}
