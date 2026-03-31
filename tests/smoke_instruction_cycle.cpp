#include <cassert>
#include <cstdint>

#include "gameboy/gameboy.hpp"
#include "machine/RegisterId.hpp"

int main()
{
    // Preserve the direct CPU contract as a reference path, not the primary host API.
    LR3592_DMG cpu;

    auto fetchBlock = cpu.fetch();
    auto execBlock = cpu.decode(fetchBlock);
    cpu.execute(execBlock, fetchBlock);

    auto& mem = cpu.getMemory();
    auto* afEntry = mem.file.findRegister(BMMQ::RegisterId::AF);
    assert(afEntry != nullptr);
    assert(afEntry->reg != nullptr);

    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->reg.get());
    assert(af != nullptr);

    // fetch() currently emits: LD A,0x12; NOP
    assert(af->hi == 0x12);

    return 0;
}
