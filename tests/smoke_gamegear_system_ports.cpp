#include "cores/gamegear/GameGearInput.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"

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
        memory.setInput(&input);
        input.reset();

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
    }

    return 0;
}
