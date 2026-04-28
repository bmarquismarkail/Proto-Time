#include "cores/gamegear/GameGearInput.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearPSG.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>

#include "machine/InputTypes.hpp"

int main()
{
    {
        GameGearInput input;
        input.reset();

        assert(input.readSystemPort0() == 0xC0u);
        assert(input.readSystemPort(0x01u) == 0x7Fu);
        assert(input.readSystemPort(0x02u) == 0xFFu);
        assert(input.readSystemPort(0x03u) == 0x00u);
        assert(input.readSystemPort(0x04u) == 0xFFu);
        assert(input.readSystemPort(0x05u) == 0x00u);
        assert(input.audioStereoControl() == 0xFFu);

        input.setLogicalButtons(BMMQ::inputButtonMask(BMMQ::InputButton::Meta2));
        assert(input.readSystemPort0() == 0x40u);

        input.writeSystemPort(0x01u, 0x12u);
        input.writeSystemPort(0x02u, 0x34u);
        input.writeSystemPort(0x03u, 0x56u);
        input.writeSystemPort(0x05u, 0x78u);
        input.writeSystemPort(0x06u, 0x9Au);

        assert(input.readSystemPort(0x01u) == 0x12u);
        assert(input.readSystemPort(0x02u) == 0x34u);
        assert(input.readSystemPort(0x03u) == 0x56u);
        assert(input.readSystemPort(0x04u) == 0xFFu);
        assert(input.readSystemPort(0x05u) == 0x78u);
        assert(input.audioStereoControl() == 0x9Au);
    }

    {
        GameGearInput input;
        GameGearMemoryMap memory;
        GameGearPSG psg;
        memory.setInput(&input);
        memory.setPsg(&psg);
        input.reset();
        psg.reset();

        assert(memory.readIoPort(0x00u) == 0xC0u);
        assert(memory.readIoPort(0x01u) == 0x7Fu);
        assert(memory.readIoPort(0x02u) == 0xFFu);
        assert(memory.readIoPort(0x03u) == 0x00u);
        assert(memory.readIoPort(0x04u) == 0xFFu);
        assert(memory.readIoPort(0x05u) == 0x00u);

        memory.writeIoPort(0x01u, 0x12u);
        memory.writeIoPort(0x02u, 0x34u);
        memory.writeIoPort(0x03u, 0x56u);
        memory.writeIoPort(0x05u, 0x78u);
        memory.writeIoPort(0x06u, 0x9Au);

        assert(memory.readIoPort(0x01u) == 0x12u);
        assert(memory.readIoPort(0x02u) == 0x34u);
        assert(memory.readIoPort(0x03u) == 0x56u);
        assert(memory.readIoPort(0x04u) == 0xFFu);
        assert(memory.readIoPort(0x05u) == 0x78u);
        assert(input.audioStereoControl() == 0x9Au);
        assert(psg.stereoControl() == 0x9Au);

        memory.writeIoPort(0x7Eu, 0x80u | 0x07u);
        memory.writeIoPort(0x7Fu, 0x03u);
        memory.writeIoPort(0x40u, 0x90u);
        assert(psg.tonePeriod(0u) == 0x037u);
        assert(psg.channelAttenuation(0u) == 0u);
    }

    {
        GameGearMemoryMap memory;
        GameGearVDP vdp;
        memory.setVdp(&vdp);
        vdp.reset();

        vdp.step(114u);
        assert(memory.readIoPort(0x7Fu) == 128u);

        memory.writeIoPort(0x3Fu, 0x05u); // TH low, output mode
        vdp.step(57u);
        assert(memory.readIoPort(0x7Fu) == 128u);

        memory.writeIoPort(0x3Eu, 0xFFu); // memory-control mirror does not affect TH
        assert(memory.readIoPort(0x7Fu) == 128u);

        memory.writeIoPort(0x3Fu, 0xFFu); // TH high, latches current H counter
        assert(memory.readIoPort(0x7Fu) == 192u);

        vdp.step(20u);
        assert(memory.readIoPort(0x7Fu) == 192u);
    }

    return 0;
}
