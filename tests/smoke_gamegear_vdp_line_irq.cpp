#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // 1) Line interrupt pending is set after expected number of scanlines
    // Set line reload = 1
    memory.writeIoPort(0xBFu, 0x01u);
    memory.writeIoPort(0xBFu, 0x8Au); // register 0x0A = 0x01
    // Ensure IE1 (line IRQ enable) is disabled for now
    memory.writeIoPort(0xBFu, 0x00u);
    memory.writeIoPort(0xBFu, 0x80u); // register 0x00 = 0x00

    // Step two scanlines: IRQ should be pending only after second step
    vdp.step(228u);
    assert(!vdp.isLineInterruptPending());
    vdp.step(228u);
    assert(vdp.isLineInterruptPending());

    // 2) IE1 gates IRQ assertion
    assert(!vdp.isIrqAsserted()); // still disabled
    // Enable IE1 (register 0 bit 4)
    memory.writeIoPort(0xBFu, 0x10u);
    memory.writeIoPort(0xBFu, 0x80u); // register 0x00 = 0x10
    assert(vdp.isIrqAsserted());
    assert(vdp.takeIrqAsserted());
    assert(vdp.takeIrqAsserted());

    // 3) Control-port read clears line interrupt pending and deasserts IRQ if none other pending
    (void)memory.readIoPort(0xBFu);
    assert(!vdp.isLineInterruptPending());
    assert(!vdp.isIrqAsserted());
    assert(!vdp.takeIrqAsserted());

    // 4) Frame interrupt and line interrupt do not incorrectly clear each other before status read
    // Reconfigure: reload=1, enable line IRQ and vblank IRQ
    vdp.reset();
    memory.setVdp(&vdp);
    vdp.reset();
    memory.writeIoPort(0xBFu, 0x01u);
    memory.writeIoPort(0xBFu, 0x8Au); // reg 0x0A = 1
    memory.writeIoPort(0xBFu, 0x10u);
    memory.writeIoPort(0xBFu, 0x80u); // reg 0x00: enable line IRQ
    memory.writeIoPort(0xBFu, 0x20u);
    memory.writeIoPort(0xBFu, 0x81u); // reg 0x01: enable vblank IRQ

    // Step until both pending are observed (line + frame)
    bool sawLine = false;
    bool sawFrame = false;
    for (int i = 0; i < 1000; ++i) {
        vdp.step(228u);
        if (vdp.isLineInterruptPending()) sawLine = true;
        if (vdp.isFrameInterruptPending()) sawFrame = true;
        if (sawLine && sawFrame) break;
    }
    assert(sawLine && sawFrame);

    // A status read should clear pending flags
    uint8_t status = memory.readIoPort(0xBFu);
    (void)status;
    assert(!vdp.isLineInterruptPending());
    assert(!vdp.isFrameInterruptPending());

    return 0;
}
