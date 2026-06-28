#include <cassert>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <stdexcept>

#include "cores/gameboy/GameBoyMemoryMap.hpp"

using GameBoyMemoryMap = GB::GameBoyMemoryMap;

int main() {
    // Test 1: Default-constructed map should export non-empty state.
    {
        GameBoyMemoryMap map;
        auto state = map.exportState();
        assert(!state.empty());
    }

    // Test 2: Export state with boot ROM active.
    {
        GameBoyMemoryMap map;
        const uint8_t dummyRom[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                     0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                     0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                                     0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                     0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F };
        map.mapBootRom(dummyRom, sizeof(dummyRom));
        assert(map.hasBootRom());
        assert(map.bootRomMapped());
        auto state = map.exportState();
        assert(!state.empty());
    }

    // Test 3: Round-trip export/import verifies data integrity.
    {
        GameBoyMemoryMap map;

        // Set some values across different memory regions.
        map.write(0x8000u, 0xAA);        // VRAM
        map.write(0xC000u, 0xBB);        // WRAM
        map.write(0xFE00u, 0xCC);        // OAM
        map.write(0xFF00u, 0xDD);        // I/O
        map.write(0xFF80u, 0xEE);        // HRAM

        auto exported = map.exportState();
        assert(!exported.empty());
        assert(exported.size() >= 2); // At least minimum size for import.

        GameBoyMemoryMap imported;
        imported.importState(exported);

        // Verify written data integrity.
        assert(imported.read(0x8000u) == 0xAA);
        assert(imported.read(0xC000u) == 0xBB);
        assert(imported.read(0xFE00u) == 0xCC);
        assert(imported.read(0xFF00u) == 0xDD);
        assert(imported.read(0xFF80u) == 0xEE);
    }

    // Test 4: Rejects truncated state (less than 2 bytes).
    {
        GameBoyMemoryMap map;
        auto exported = map.exportState();
        assert(!exported.empty());
        assert(exported.size() >= 2); // Full export must be >= 2 bytes.

        // Importing with less than 2 bytes is the rejection case.
        GameBoyMemoryMap imported;
        try {
            imported.importState({0x01});
            assert(false); // Should have thrown
        } catch (const std::invalid_argument&) {
            // Expected
        }
        try {
            imported.importState({});
            assert(false); // Should have thrown
        } catch (const std::invalid_argument&) {
            // Expected
        }

        std::vector<uint8_t> shortTrailer{0x00, 0x01, 0x02};
        try {
            imported.importState(shortTrailer);
            assert(false); // Should have thrown
        } catch (const std::invalid_argument&) {
            // Expected
        }

        auto corrupt = exported;
        corrupt[corrupt.size() - 1u] ^= 0xFFu;
        try {
            imported.importState(corrupt);
            assert(false); // Should have thrown
        } catch (const std::invalid_argument&) {
            // Expected
        }
    }

    // Test 5: Boot ROM payload persists when explicitly mapped.
    {
        GameBoyMemoryMap map;
        const uint8_t dummyRom[] = {0x31, 0xFE, 0xFF};
        map.mapBootRom(dummyRom, sizeof(dummyRom));
        auto exported = map.exportState();

        GameBoyMemoryMap imported;
        imported.importState(exported);
        assert(imported.hasBootRom());
        assert(imported.bootRomMapped());
        assert(imported.read(0x0000u) == dummyRom[0]);
    }

    return 0;
}
