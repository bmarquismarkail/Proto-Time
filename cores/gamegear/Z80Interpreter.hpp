#pragma once
// Sega Game Gear Z80 CPU interpreter stub
// References: SMS Power, MAME, Emulicious, Genesis Plus GX

#include <cstdint>

class Z80Interpreter {
public:
    Z80Interpreter();
    ~Z80Interpreter();

    void reset();
    void step();
    // TODO: Add register/memory interface
};
