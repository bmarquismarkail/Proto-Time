#include <cassert>
#include <cstdint>

#include "gameboy/gameboy.hpp"

int main()
{
    // Preserve the direct CPU contract as a reference path, not the primary host API.
    LR3592_DMG cpu;

    auto fetchBlock = cpu.fetch();
    auto execBlock = cpu.decode(fetchBlock);
    cpu.execute(execBlock, fetchBlock);

    auto& mem = cpu.getMemory();
    auto* afEntry = mem.file.findRegister("AF");
    assert(afEntry != nullptr);
    assert(afEntry->second != nullptr);

    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->second);
    assert(af != nullptr);

    // fetch() currently emits: LD A,0x12; NOP
    assert(af->hi == 0x12);

    return 0;
}
