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

    vdp.step(228u * 193u);
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

    {
        GameGearVDP docVdp;
        GameGearMemoryMap docMemory;
        docMemory.setVdp(&docVdp);
        docVdp.reset();

        docMemory.writeIoPort(0xBFu, 0xFEu);
        docMemory.writeIoPort(0xBFu, 0x81u); // register 1: display on
        docMemory.writeIoPort(0xBFu, 0x77u);
        docMemory.writeIoPort(0xBFu, 0x8Bu); // register 11 ignored
        assert(docMemory.read(0xFF4Bu) == 0xFFu);

        docMemory.writeIoPort(0xBFu, 0xFFu);
        docMemory.writeIoPort(0xBFu, 0x7Fu); // VRAM write at 0x3FFF
        docMemory.writeIoPort(0xBEu, 0xA5u);
        docMemory.writeIoPort(0xBEu, 0x5Au); // wraps to 0x0000
        assert(docVdp.readVram(0xBFFFu) == 0xA5u);
        assert(docVdp.readVram(0x8000u) == 0x5Au);

        docMemory.writeIoPort(0xBFu, 0x34u);
        docMemory.writeIoPort(0xBFu, 0x92u); // register write also loads address 0x1234 and mode VRAM write
        docMemory.writeIoPort(0xBEu, 0xC3u);
        assert(docVdp.readVram(0x9234u) == 0xC3u);

        docMemory.writeIoPort(0xBFu, 0x01u);
        docMemory.writeIoPort(0xBFu, 0x8Au); // line counter reload value = 1
        docMemory.writeIoPort(0xBFu, 0x10u);
        docMemory.writeIoPort(0xBFu, 0x80u); // line interrupt enable
        assert(!docVdp.takeIrqAsserted());
        docVdp.step(228u);
        assert(!docVdp.takeIrqAsserted());
        docVdp.step(228u);
        assert(docVdp.takeIrqAsserted());
        (void)docMemory.readIoPort(0xBFu);
        assert(!docVdp.takeIrqAsserted());

        // Sprite Y coordinate is stored as screen Y - 1, and bit 3 of register
        // 0 shifts sprites left by 8 pixels. Bit 0 of register 1 doubles sprite
        // pixels on GG/SMS2 VDPs.
        docMemory.writeIoPort(0xBFu, 0x40u);
        docMemory.writeIoPort(0xBFu, 0x81u); // display on, no zoom
        docMemory.writeIoPort(0xBFu, 0xFFu);
        docMemory.writeIoPort(0xBFu, 0x85u); // SAT 0x3F00
        docMemory.writeIoPort(0xBFu, 0xFFu);
        docMemory.writeIoPort(0xBFu, 0x86u); // sprite patterns 0x2000
        docMemory.writeIoPort(0xBFu, 0x22u);
        docMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color1
        docMemory.writeIoPort(0xBEu, 0x0Fu);
        docMemory.writeIoPort(0xBEu, 0x00u);
        docMemory.writeIoPort(0xBFu, 0x20u);
        docMemory.writeIoPort(0xBFu, 0x60u); // tile 1 at 0x2020
        for (int row = 0; row < 8; ++row) {
            docMemory.writeIoPort(0xBEu, 0xFFu);
            docMemory.writeIoPort(0xBEu, 0x00u);
            docMemory.writeIoPort(0xBEu, 0x00u);
            docMemory.writeIoPort(0xBEu, 0x00u);
        }
        docMemory.writeIoPort(0xBFu, 0x00u);
        docMemory.writeIoPort(0xBFu, 0x7Fu); // SAT y
        docMemory.writeIoPort(0xBEu, 23u);   // appears on screen line 24
        docMemory.writeIoPort(0xBEu, 0xD0u);
        docMemory.writeIoPort(0xBFu, 0x80u);
        docMemory.writeIoPort(0xBFu, 0x7Fu); // SAT x/tile
        docMemory.writeIoPort(0xBEu, 24u);
        docMemory.writeIoPort(0xBEu, 0x01u);
        auto spriteModel = docVdp.buildFrameModel({160, 144});
        assert(spriteModel.argbPixels[24u * 160u + 24u] != spriteModel.argbPixels[0]);
        assert(spriteModel.argbPixels[23u * 160u + 24u] == spriteModel.argbPixels[0]);

        docMemory.writeIoPort(0xBFu, 0x08u);
        docMemory.writeIoPort(0xBFu, 0x80u); // early clock
        auto shiftedSpriteModel = docVdp.buildFrameModel({160, 144});
        assert(shiftedSpriteModel.argbPixels[24u * 160u + 16u] != shiftedSpriteModel.argbPixels[0]);
        docMemory.writeIoPort(0xBFu, 0x41u);
        docMemory.writeIoPort(0xBFu, 0x81u); // display on + zoom
        auto zoomedSpriteModel = docVdp.buildFrameModel({160, 144});
        assert(zoomedSpriteModel.argbPixels[24u * 160u + 31u] != zoomedSpriteModel.argbPixels[0]);
        assert(zoomedSpriteModel.argbPixels[39u * 160u + 16u] != zoomedSpriteModel.argbPixels[0]);
    }

    return 0;
}
