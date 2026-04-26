#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace


int main()
{
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // --- VDP CONTROL/DATA PORT BEHAVIOR TESTS ---
    // 1. VDP control port uses a two-byte command latch
    {
        vdp.reset();
        // Write only one byte: should not change VRAM
        memory.writeIoPort(0xBFu, 0x34u); // first byte
        vdp.writeVram(0x9234u, 0x00u); // clear for test
        memory.writeIoPort(0xBEu, 0xAAu); // data port write (should not write to VRAM at 0x9234)
        assert(vdp.readVram(0x9234u) == 0x00u); // no effect, latch not completed
        // The data-port write clears the latch, so send a fresh two-byte command.
        memory.writeIoPort(0xBFu, 0x34u);
        memory.writeIoPort(0xBFu, 0x52u); // second byte (VRAM write, address 0x1234)
        memory.writeIoPort(0xBEu, 0xBBu); // data port write
        assert(vdp.readVram(0x9234u) == 0xBBu); // VRAM written
    }


    // 2. Reading control port clears the command latch
    {
        vdp.reset();
        memory.writeIoPort(0xBFu, 0x12u); // first byte
        assert(vdp.isCommandLatchPending()); // internal: latch should be set
        (void)memory.readIoPort(0xBFu); // read control port
        assert(!vdp.isCommandLatchPending()); // latch cleared
    }

    // 3. Reading data port clears the command latch
    {
        vdp.reset();
        memory.writeIoPort(0xBFu, 0x12u); // first byte
        assert(vdp.isCommandLatchPending());
        (void)memory.readIoPort(0xBEu); // read data port
        assert(!vdp.isCommandLatchPending());
    }

    // 4. Writing data port clears the command latch
    {
        vdp.reset();
        memory.writeIoPort(0xBFu, 0x12u); // first byte
        assert(vdp.isCommandLatchPending());
        memory.writeIoPort(0xBEu, 0x55u); // write data port
        assert(!vdp.isCommandLatchPending());
    }

    // 5. VRAM reads are buffered
    {
        vdp.reset();
        // Write known values
        vdp.writeVram(0x9034u, 0xDEu);
        vdp.writeVram(0x9035u, 0xADu);
        // Set up VRAM read at 0x1034
        memory.writeIoPort(0xBFu, 0x34u);
        memory.writeIoPort(0xBFu, 0x10u); // VRAM read, address 0x1034
        // The read command preloads the buffer from 0x1034.
        uint8_t first = memory.readIoPort(0xBEu);
        // The first data read queues 0x1035 for the next read.
        uint8_t second = memory.readIoPort(0xBEu);
        // The second data read queues 0x1036, which is still zero.
        uint8_t third = memory.readIoPort(0xBEu);
        assert(first == 0xDEu);
        assert(second == 0xADu);
        assert(third == 0u);
    }

    // 6. VRAM writes update the read buffer
    {
        vdp.reset();
        memory.writeIoPort(0xBFu, 0x34u);
        memory.writeIoPort(0xBFu, 0x41u); // VRAM write, address 0x1134
        memory.writeIoPort(0xBEu, 0x99u); // write data port
        // Now, read data port: buffer should return 0x99
        uint8_t buf = memory.readIoPort(0xBEu);
        assert(buf == 0x99u);
    }

    // 7. VDP address auto-increments and wraps at 0x3FFF
    {
        vdp.reset();
        // Set address to 0x3FFF, VRAM write
        memory.writeIoPort(0xBFu, 0xFFu);
        memory.writeIoPort(0xBFu, 0x7Fu); // VRAM write, address 0x3FFF
        memory.writeIoPort(0xBEu, 0x77u); // write data port
        // Next write should wrap to 0x0000
        memory.writeIoPort(0xBEu, 0x88u);
        assert(vdp.readVram(0xBFFFu) == 0x77u);
        assert(vdp.readVram(0x8000u) == 0x88u);
    }

    // 8. Control-port read clears status/IRQ pending flags
    {
        vdp.reset();
        // Simulate VBlank IRQ
        // Use a helper to set the private flags via a control port read
        // Instead, use public interface: simulate by stepping to vblank
        // (But for test, we can use a friend or test-only setter if needed)
        // Here, we use a workaround: set up a vblank
        vdp.step(228u * 193u); // step to vblank
        assert(vdp.isFrameInterruptPending() || vdp.isIrqAsserted());
        uint8_t status = memory.readIoPort(0xBFu);
        assert((status & 0x80u) != 0u); // IRQ flag set
        // After read, flags should be cleared
        assert(!vdp.isFrameInterruptPending());
        assert(!vdp.isIrqAsserted());
    }

    // ...existing code...

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

    memory.writeIoPort(0xBFu, 0x00u);
    memory.writeIoPort(0xBFu, 0x81u); // display off, expose backdrop color
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
        assert(docMemory.readIoPort(0xBEu) == 0xA5u);
        docMemory.writeIoPort(0xBFu, 0x00u);
        docMemory.writeIoPort(0xBFu, 0x40u); // restore VRAM write at 0x0000
        docMemory.writeIoPort(0xBEu, 0x5Au); // wraps to 0x0000
        assert(docVdp.readVram(0xBFFFu) == 0xA5u);
        assert(docVdp.readVram(0x8000u) == 0x5Au);

        docMemory.writeIoPort(0xBFu, 0x04u);
        docMemory.writeIoPort(0xBFu, 0xC0u); // CRAM write
        docMemory.writeIoPort(0xBEu, 0x3Cu);
        assert(docMemory.readIoPort(0xBEu) == 0x3Cu);

        docMemory.writeIoPort(0xBFu, 0x34u);
        docMemory.writeIoPort(0xBFu, 0x92u); // register write also loads address 0x1234 and mode VRAM write
        docMemory.writeIoPort(0xBEu, 0xC3u);
        assert(docVdp.readVram(0x9234u) == 0xC3u);

        docMemory.writeIoPort(0xBFu, 0x34u);
        docMemory.writeIoPort(0xBFu, 0x52u); // VRAM write address 0x1234
        docMemory.writeIoPort(0xBFu, 0xABu); // first control byte updates A0-A7 immediately
        docMemory.writeIoPort(0xBEu, 0x6Du); // data write clears latch and uses 0x12AB
        assert(docVdp.readVram(0x9234u) == 0xC3u);
        assert(docVdp.readVram(0x92ABu) == 0x6Du);

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

        // Sprite Y coordinate is stored as VDP Y - 1, and bit 3 of register
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
        docMemory.writeIoPort(0xBEu, 47u);   // appears on LCD line 24
        docMemory.writeIoPort(0xBEu, 0xD0u);
        docMemory.writeIoPort(0xBFu, 0x80u);
        docMemory.writeIoPort(0xBFu, 0x7Fu); // SAT x/tile
        docMemory.writeIoPort(0xBEu, 72u);
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

    {
        GameGearVDP viewportVdp;
        GameGearMemoryMap viewportMemory;
        viewportMemory.setVdp(&viewportVdp);
        viewportVdp.reset();

        viewportMemory.writeIoPort(0xBFu, 0x40u);
        viewportMemory.writeIoPort(0xBFu, 0x81u); // display on
        viewportMemory.writeIoPort(0xBFu, 0xFFu);
        viewportMemory.writeIoPort(0xBFu, 0x85u); // SAT 0x3F00
        viewportMemory.writeIoPort(0xBFu, 0xFFu);
        viewportMemory.writeIoPort(0xBFu, 0x86u); // sprite patterns 0x2000
        viewportMemory.writeIoPort(0xBFu, 0x02u);
        viewportMemory.writeIoPort(0xBFu, 0x87u); // backdrop color code 2

        viewportMemory.writeIoPort(0xBFu, 0x22u);
        viewportMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color1
        viewportMemory.writeIoPort(0xBEu, 0x0Fu);
        viewportMemory.writeIoPort(0xBEu, 0x00u);
        viewportMemory.writeIoPort(0xBFu, 0x20u);
        viewportMemory.writeIoPort(0xBFu, 0x60u); // sprite tile 1 at 0x2020
        for (int row = 0; row < 8; ++row) {
            viewportMemory.writeIoPort(0xBEu, 0xFFu);
            viewportMemory.writeIoPort(0xBEu, 0x00u);
            viewportMemory.writeIoPort(0xBEu, 0x00u);
            viewportMemory.writeIoPort(0xBEu, 0x00u);
        }
        viewportMemory.writeIoPort(0xBFu, 0x00u);
        viewportMemory.writeIoPort(0xBFu, 0x7Fu); // SAT y at VDP y 24
        viewportMemory.writeIoPort(0xBEu, 23u);
        viewportMemory.writeIoPort(0xBEu, 0xD0u);
        viewportMemory.writeIoPort(0xBFu, 0x80u);
        viewportMemory.writeIoPort(0xBFu, 0x7Fu); // SAT x/tile at VDP x 48
        viewportMemory.writeIoPort(0xBEu, 48u);
        viewportMemory.writeIoPort(0xBEu, 0x01u);

        const auto viewportModel = viewportVdp.buildFrameModel({160, 144});
        if (viewportModel.argbPixels.empty()) {
            return fail("Game Gear viewport model unexpectedly empty");
        }
        const auto viewportSpritePixel = viewportModel.argbPixels[0];
        const auto viewportBackdropPixel = viewportModel.argbPixels[8u];
        if (viewportSpritePixel == viewportBackdropPixel) {
            return fail("Game Gear viewport did not map VDP coordinate (48,24) to LCD coordinate (0,0)");
        }
        if (viewportModel.argbPixels[24u * 160u + 48u] != viewportBackdropPixel) {
            return fail("Game Gear viewport left sprite visible at unshifted VDP coordinate");
        }
    }

    {
        GameGearVDP colorZeroVdp;
        GameGearMemoryMap colorZeroMemory;
        colorZeroMemory.setVdp(&colorZeroVdp);
        colorZeroVdp.reset();

        colorZeroMemory.writeIoPort(0xBFu, 0x40u);
        colorZeroMemory.writeIoPort(0xBFu, 0x81u); // display on
        colorZeroMemory.writeIoPort(0xBFu, 0x00u);
        colorZeroMemory.writeIoPort(0xBFu, 0x87u); // backdrop color code 0
        colorZeroMemory.writeIoPort(0xBFu, 0x00u);
        colorZeroMemory.writeIoPort(0xBFu, 0xC0u); // background palette color 0
        colorZeroMemory.writeIoPort(0xBEu, 0xFFu);
        colorZeroMemory.writeIoPort(0xBEu, 0x0Fu);
        colorZeroMemory.writeIoPort(0xBFu, 0x20u);
        colorZeroMemory.writeIoPort(0xBFu, 0xC0u); // sprite/backdrop palette color 0
        colorZeroMemory.writeIoPort(0xBEu, 0x00u);
        colorZeroMemory.writeIoPort(0xBEu, 0x00u);

        const auto colorZeroModel = colorZeroVdp.buildFrameModel({160, 144});
        if (colorZeroModel.argbPixels.empty()) {
            return fail("Game Gear color-zero model unexpectedly empty");
        }
        if (colorZeroModel.argbPixels[0] != 0xFFFFFFFFu) {
            return fail("Game Gear background color 0 did not render from background palette color 0");
        }
    }

    {
        GameGearVDP tmsVdp;
        GameGearMemoryMap tmsMemory;
        tmsMemory.setVdp(&tmsVdp);
        tmsVdp.reset();
        tmsVdp.setSmsMode(true);

        tmsMemory.writeIoPort(0xBFu, 0x02u);
        tmsMemory.writeIoPort(0xBFu, 0x80u); // Graphics II, not Mode 4
        tmsMemory.writeIoPort(0xBFu, 0x40u);
        tmsMemory.writeIoPort(0xBFu, 0x81u); // display on
        tmsMemory.writeIoPort(0xBFu, 0x02u);
        tmsMemory.writeIoPort(0xBFu, 0x82u); // name table 0x0800
        tmsMemory.writeIoPort(0xBFu, 0x80u);
        tmsMemory.writeIoPort(0xBFu, 0x83u); // color table 0x2000
        tmsMemory.writeIoPort(0xBFu, 0x00u);
        tmsMemory.writeIoPort(0xBFu, 0x84u); // pattern table 0x0000

        tmsMemory.writeIoPort(0xBFu, 0x11u);
        tmsMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color1: white
        tmsMemory.writeIoPort(0xBEu, 0x3Fu);
        tmsMemory.writeIoPort(0xBFu, 0x12u);
        tmsMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color2: blue
        tmsMemory.writeIoPort(0xBEu, 0x30u);

        tmsMemory.writeIoPort(0xBFu, 0x08u);
        tmsMemory.writeIoPort(0xBFu, 0x60u); // color table tile 1, row 0
        tmsMemory.writeIoPort(0xBEu, 0x12u); // foreground color 1, background color 2
        tmsMemory.writeIoPort(0xBFu, 0x08u);
        tmsMemory.writeIoPort(0xBFu, 0x40u); // pattern tile 1, row 0
        tmsMemory.writeIoPort(0xBEu, 0x80u); // leftmost pixel set
        tmsMemory.writeIoPort(0xBFu, 0x61u);
        tmsMemory.writeIoPort(0xBFu, 0x48u); // name table tile at VDP (8,24)
        tmsMemory.writeIoPort(0xBEu, 0x01u);

        const auto tmsModel = tmsVdp.buildFrameModel({160, 144});
        if (tmsModel.argbPixels.empty()) {
            return fail("Game Gear TMS Graphics II model unexpectedly empty");
        }
        if (tmsModel.argbPixels[0] != 0xFFFFFFFFu) {
            return fail("Game Gear TMS Graphics II foreground pixel did not use CRAM color 17");
        }
        if (tmsModel.argbPixels[1] != 0xFF0000FFu) {
            return fail("Game Gear TMS Graphics II background pixel did not use CRAM color 18");
        }
    }

    {
        GameGearVDP tmsViewportVdp;
        GameGearMemoryMap tmsViewportMemory;
        tmsViewportMemory.setVdp(&tmsViewportVdp);
        tmsViewportVdp.reset();
        tmsViewportVdp.setSmsMode(true);

        tmsViewportMemory.writeIoPort(0xBFu, 0x02u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x80u); // Graphics II, not Mode 4
        tmsViewportMemory.writeIoPort(0xBFu, 0x40u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x81u); // display on
        tmsViewportMemory.writeIoPort(0xBFu, 0x02u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x82u); // name table 0x0800
        tmsViewportMemory.writeIoPort(0xBFu, 0x80u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x83u); // color table 0x2000
        tmsViewportMemory.writeIoPort(0xBFu, 0x00u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x84u); // pattern table 0x0000

        tmsViewportMemory.writeIoPort(0xBFu, 0x11u);
        tmsViewportMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color1: white
        tmsViewportMemory.writeIoPort(0xBEu, 0x3Fu);
        tmsViewportMemory.writeIoPort(0xBFu, 0x12u);
        tmsViewportMemory.writeIoPort(0xBFu, 0xC0u); // palette1 color2: blue
        tmsViewportMemory.writeIoPort(0xBEu, 0x30u);

        tmsViewportMemory.writeIoPort(0xBFu, 0x08u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x60u); // color table tile 1, row 0
        tmsViewportMemory.writeIoPort(0xBEu, 0x12u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x08u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x40u); // pattern tile 1, row 0
        tmsViewportMemory.writeIoPort(0xBEu, 0x80u); // leftmost pixel set
        tmsViewportMemory.writeIoPort(0xBFu, 0x61u);
        tmsViewportMemory.writeIoPort(0xBFu, 0x48u); // name table tile at VDP (8,24)
        tmsViewportMemory.writeIoPort(0xBEu, 0x01u);

        const auto tmsViewportModel = tmsViewportVdp.buildFrameModel({160, 144});
        if (tmsViewportModel.argbPixels.empty()) {
            return fail("Game Gear TMS compatibility viewport model unexpectedly empty");
        }
        if (tmsViewportModel.argbPixels[0] != 0xFFFFFFFFu) {
            return fail("Game Gear TMS compatibility viewport did not map LCD x=0 to SMS x=8");
        }
        if (tmsViewportModel.argbPixels[1] != 0xFF0000FFu) {
            return fail("Game Gear TMS compatibility viewport did not horizontally compress SMS pixels");
        }
    }

    return 0;
}
