#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    memory.writeIoPort(0xBFu, 0x40u);
    memory.writeIoPort(0xBFu, 0x81u); // display on
    memory.writeIoPort(0xBFu, 0x01u);
    memory.writeIoPort(0xBFu, 0x87u); // backdrop color code 1

    vdp.step(228u * 144u);
    const auto firstStatus = memory.readIoPort(0xBFu);
    assert((firstStatus & 0x80u) != 0u);
    const auto secondStatus = memory.readIoPort(0xBFu);
    assert((secondStatus & 0x80u) == 0u);

    memory.writeIoPort(0x81u, 0x05u);
    memory.writeIoPort(0x81u, 0x83u); // mirrored control-port register write
    assert(memory.read(0xFF43u) == 0x05u);

    assert(memory.readIoPort(0x7Eu) == vdp.readVCounter());
    assert(memory.readIoPort(0x7Fu) == vdp.readHCounter());

    memory.writeIoPort(0xBFu, 0x22u);
    memory.writeIoPort(0xBFu, 0xC0u); // CRAM addr 0x22
    const auto before = vdp.buildFrameModel({160, 144});
    memory.writeIoPort(0xBEu, 0x00u); // even write only latches
    const auto afterEvenWrite = vdp.buildFrameModel({160, 144});
    assert(afterEvenWrite.argbPixels == before.argbPixels);
    memory.writeIoPort(0xBFu, 0x23u);
    memory.writeIoPort(0xBFu, 0xC0u); // odd byte for same palette entry
    memory.writeIoPort(0xBEu, 0x0Fu);
    const auto committedDifferent = vdp.buildFrameModel({160, 144});
    assert(committedDifferent.argbPixels != before.argbPixels);

    return 0;
}
