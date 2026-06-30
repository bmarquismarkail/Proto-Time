#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/GameBoyInput.hpp"

using GameBoyInput = GB::GameBoyInput;

int main()
{
    // Test 1: Default state (no buttons pressed, no joystick selection) exports valid data.
    {
        GameBoyInput input;
        auto state = input.exportState();
        assert(!state.empty());
        assert(state.size() == 2u);
    }

    // Test 2: Export and import back matches default state.
    {
        GameBoyInput input;
        auto exported = input.exportState();
        GameBoyInput imported;
        imported.importState(exported);
        auto reExported = imported.exportState();
        assert(reExported == exported);
    }

    // Test 3: Set buttons to some combination, exercise JOYP select bits, export/import, verify.
    {
        GameBoyInput input;

        // Select the directional group only so the low nibble reads D-pad state.
        input.writeRegister(0x20u); // JOYP bit5=1 deselects buttons, bit4=0 selects directions

        // Set logical buttons: Up + A + B.
        input.setLogicalButtons(GameBoyInput::kUp | GameBoyInput::kA | GameBoyInput::kB);

        auto exported = input.exportState();
        assert(!exported.empty());
        assert(exported.size() == 2u);

        const uint8_t physical = input.readRegister();
        assert((physical & 0x04u) == 0); // Up pressed.
        assert((physical & 0x01u) != 0); // A not visible in directional view.
        assert((physical & 0x02u) != 0); // B not visible in directional view.
        assert((physical & 0x08u) != 0); // Down not pressed in selected button low nibble.

        // Import back and verify state matches, especially preserved select bits.
        GameBoyInput imported;
        imported.importState(exported);

        // select-bit persistence pass: read back reflects the exported register state.
        assert(imported.readRegister() == physical);

        auto reExported = imported.exportState();
        assert(reExported == exported);
    }

    // Test 4: Set buttons to another combination and round-trip again.
    {
        GameBoyInput input;
        // Set: Down + Select + Start.
        input.setLogicalButtons(
            GameBoyInput::kDown | GameBoyInput::kSelect | GameBoyInput::kStart);

        auto exported = input.exportState();
        assert(exported.size() == 2u);
        GameBoyInput imported;
        imported.importState(exported);

        auto reExported = imported.exportState();
        assert(reExported == exported);

        input.writeRegister(0x00u);
        const uint8_t physical = input.readRegister();
        assert((physical & 0x08u) == 0); // Down pressed.
        assert((physical & 0x04u) == 0); // Select pressed.
        assert((physical & 0x08u) == 0); // Start shares the selected low nibble bit.
        assert((physical & 0x01u) != 0); // A not pressed.
        assert((physical & 0x02u) != 0); // B not pressed.

        // Import back again to ensure idempotency.
        GameBoyInput imported2;
        imported2.importState(exported);
        auto reExported2 = imported2.exportState();
        assert(reExported2 == exported);
        imported2.writeRegister(0x00u);
        assert(imported2.readRegister() == physical);
    }

    // Test 5: malformed state sizes are rejected.
    {
        GameBoyInput input;
        bool threw = false;
        try {
            input.importState(std::vector<uint8_t>{0x01u});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);

        threw = false;
        try {
            input.importState(std::vector<uint8_t>{0x01u, 0x02u, 0x03u});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    }

    return 0;
}
