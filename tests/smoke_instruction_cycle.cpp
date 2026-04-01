#include <cassert>
#include <cstdint>

#include "gameboy/gameboy.hpp"
#include "machine/RegisterId.hpp"

int main()
{
    // Preserve the direct CPU contract as a reference path, not the primary host API.
    LR3592_DMG cpu;

    cpu.loadProgram({0x00, 0xC3, 0x34, 0x12}, 0x0000);
    auto* pcEntry = cpu.getMemory().file.findRegister(BMMQ::RegisterId::PC);
    assert(pcEntry != nullptr);
    assert(pcEntry->reg != nullptr);
    pcEntry->reg->value = 0x0000;

    auto fetchBlock = cpu.fetch();
    assert(fetchBlock.getblockData().size() == 1);
    assert(fetchBlock.getblockData()[0].data.size() == 1);
    assert(fetchBlock.getblockData()[0].data[0] == 0x00);

    auto execBlock = cpu.decode(fetchBlock);
    cpu.execute(execBlock, fetchBlock);

    auto& mem = cpu.getMemory();
    auto* afEntry = mem.file.findRegister(BMMQ::RegisterId::AF);
    assert(afEntry != nullptr);
    assert(afEntry->reg != nullptr);

    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->reg.get());
    assert(af != nullptr);

    assert(af->hi == 0x00);
    assert(pcEntry->reg->value == 0x0001);

    auto secondFetchBlock = cpu.fetch();
    assert(secondFetchBlock.getblockData().size() == 1);
    assert(secondFetchBlock.getblockData()[0].data.size() == 3);
    assert(secondFetchBlock.getblockData()[0].data[0] == 0xC3);

    auto secondExecBlock = cpu.decode(secondFetchBlock);
    cpu.execute(secondExecBlock, secondFetchBlock);
    assert(pcEntry->reg->value == 0x1234);

    return 0;
}
