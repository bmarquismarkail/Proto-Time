// Smoke test: Game Gear RAM mirror and mapper register readback
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearCartridge.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    GameGearCartridge cartridge;
    GameGearMemoryMap memory;
    memory.setCartridge(&cartridge);
    memory.reset();

    // Write to RAM at $C000-$C003
    for (uint16_t i = 0; i < 4; ++i) {
        memory.write(0xC000u + i, 0xA0u + i);
        assert(memory.read(0xC000u + i) == 0xA0u + i);
        assert(memory.read(0xE000u + i) == 0xA0u + i); // mirror
    }
    // Write to RAM at $DFFC-$DFFF
    for (uint16_t i = 0; i < 4; ++i) {
        memory.write(0xDFFCu + i, 0xB0u + i);
        assert(memory.read(0xDFFCu + i) == 0xB0u + i);
        assert(memory.read(0xFFFCu + i) == 0xB0u + i); // mirror
    }
    // Write to RAM at $FFFC-$FFFF, check $DFFC-$DFFF
    for (uint16_t i = 0; i < 4; ++i) {
        memory.write(0xFFFCu + i, 0xC0u + i);
        assert(memory.read(0xFFFCu + i) == 0xC0u + i);
        assert(memory.read(0xDFFCu + i) == 0xC0u + i); // mirror
    }
    // Mapper register writes update RAM mirror
    memory.write(0xFFFCu, 0x55u);
    memory.write(0xFFFDu, 0x66u);
    memory.write(0xFFFEu, 0x77u);
    memory.write(0xFFFFu, 0x88u);
    assert(memory.read(0xFFFCu) == 0x55u);
    assert(memory.read(0xFFFDu) == 0x66u);
    assert(memory.read(0xFFFEu) == 0x77u);
    assert(memory.read(0xFFFFu) == 0x88u);
    assert(memory.read(0xDFFCu) == 0x55u);
    assert(memory.read(0xDFFDu) == 0x66u);
    assert(memory.read(0xDFFEu) == 0x77u);
    assert(memory.read(0xDFFFu) == 0x88u);
    // RAM mirror is not broken by mapper writes
    memory.write(0xDFFCu, 0x99u);
    assert(memory.read(0xFFFCu) == 0x99u);
    assert(memory.read(0xDFFCu) == 0x99u);
    return 0;
}
