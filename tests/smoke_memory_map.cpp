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

    BMMQ::MemoryMap fullRomMap;
    fullRomMap.mapRom(0x0000, 0x4000);
    fullRomMap.mapRom(0x4000, 0x4000);
    std::vector<uint8_t> fullRom(0x8000);
    fullRom.front() = 0xAA;
    fullRom[0x3FFF] = 0xBB;
    fullRom[0x4000] = 0xCC;
    fullRom.back() = 0xDD;
    fullRomMap.installRom(fullRom, 0x0000);

    assert(fullRomMap.read8(0x0000) == 0xAA);
    assert(fullRomMap.read8(0x3FFF) == 0xBB);
    assert(fullRomMap.read8(0x4000) == 0xCC);
    assert(fullRomMap.read8(0x7FFF) == 0xDD);

    return 0;
}
