#include "cores/gamegear/GameGearCartridge.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/mappers/CodemastersMapper.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

int main()
{
    GameGearCartridge cartridge;
    GameGearMemoryMap memory;
    memory.setCartridge(&cartridge);

    const std::size_t page = 0x4000u;
    std::vector<uint8_t> rom(page * 4u);
    for (std::size_t i = 0; i < page; ++i) rom[i] = 0x10u;
    for (std::size_t i = 0; i < page; ++i) rom[page + i] = 0x20u;
    for (std::size_t i = 0; i < page; ++i) rom[page * 2u + i] = 0x30u;
    for (std::size_t i = 0; i < page; ++i) rom[page * 3u + i] = 0x40u;

    assert(cartridge.load(rom.data(), rom.size()));
    memory.reset();

    assert(memory.read(0x0000u) == 0x10u);
    assert(memory.read(0x4000u) == 0x20u);
    assert(memory.read(0x8000u) == 0x30u);

    memory.write(0xFFFFu, 0x07u); // wraps to bank 3 for page 2
    assert(memory.read(0x8000u) == 0x40u);

    memory.write(0xFFFCu, 0x08u); // SRAM bank 0 enable
    memory.write(0x8000u, 0xA5u);
    memory.write(0xBFFFu, 0x5Au);
    assert(memory.read(0x8000u) == 0xA5u);
    assert(memory.read(0xBFFFu) == 0x5Au);

    memory.write(0xFFFCu, 0x0Cu); // SRAM bank 1 enable
    assert(memory.read(0x8000u) == 0x00u);
    memory.write(0x8000u, 0x3Cu);
    assert(memory.read(0x8000u) == 0x3Cu);

    memory.write(0xFFFCu, 0x08u);
    assert(memory.read(0x8000u) == 0xA5u);

    memory.write(0xFFFCu, 0x00u); // disable SRAM, ROM visible again
    assert(memory.read(0x8000u) == 0x40u);

    memory.write(0x8000u, 0x99u); // ROM window writes ignored when SRAM disabled
    assert(memory.read(0x8000u) == 0x40u);

    CodemastersMapper mapper;
    assert(mapper.load(rom.data(), rom.size()));
    mapper.write(0x4000,0x81); // Enable Codemasters' distinct 8 KiB RAM.
    const auto banksBeforeRamWrite=mapper.bankRegisters();
    mapper.write(0xa000,0x5a);mapper.write(0xbfff,0x3c);
    assert(!mapper.handlesControlWrite(0xa000)&&mapper.handlesMappedWrite(0xa000));
    assert(mapper.bankRegisters()==banksBeforeRamWrite);
    assert(mapper.read(0xa000)==0x5a&&mapper.read(0xbfff)==0x3c);
    assert(mapper.hasDirtySaveData());
    const auto save=mapper.exportSaveData();assert(save.size()==8192&&save.front()==0x5a&&save.back()==0x3c);
    mapper.markSaveClean();assert(!mapper.hasDirtySaveData());
    mapper.write(0x4000,1);assert(mapper.handlesControlWrite(0xa000));
    mapper.write(0x4000,0x81);assert(mapper.read(0xa000)==0x5a);
    auto mapperState = mapper.exportState();
    assert(mapperState.size() >= 2u);
    mapperState[mapperState.size() - 1u] = 2u;
    bool rejectedInvalidFlag = false;
    try {
        mapper.importState(mapperState);
    } catch (const std::invalid_argument&) {
        rejectedInvalidFlag = true;
    }
    assert(rejectedInvalidFlag);

    return 0;
}
