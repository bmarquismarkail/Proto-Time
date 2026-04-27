#include "cores/gamegear/Z80Interpreter.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

Z80Interpreter makeCpu(std::vector<uint8_t>& memory) {
    Z80Interpreter cpu;
    cpu.setMemoryInterface(
        [&](uint16_t address) { return memory[address]; },
        [&](uint16_t address, uint8_t value) { memory[address] = value; }
    );
    cpu.reset();
    return cpu;
}

#define CHECK_OR_FAIL(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "smoke_gamegear_z80_fd_indexed: " << message << '\n'; \
            return 1; \
        } \
    } while (false)

}

int main() {
    // FD 6E: LD L,(IY+d) -- should load into L (HL low) and NOT mutate IY
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        // Setup IY = 0x1234
        memory[0x0000u] = 0xFDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x34u; memory[0x0003u] = 0x12u; // LD IY,0x1234
        // LD L,(IY+2)
        memory[0x0004u] = 0xFDu; memory[0x0005u] = 0x6Eu; memory[0x0006u] = 0x02u;

        // place value at IY+2 (0x1236)
        memory[0x1236u] = 0xABu;

        CHECK_OR_FAIL(cpu.step() == 14u, "LD IY,nn setup failed");
        CHECK_OR_FAIL(cpu.step() == 19u, "FD6E LD L,(IY+d) should take 19 cycles");
        CHECK_OR_FAIL(cpu.IY == 0x1234u, "FD6E should not mutate IY");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.HL & 0x00FFu) == 0xABu, "FD6E should load value into L (HL low)");
    }

    // FD 66: LD H,(IY+d) -- should load into H (HL high) and NOT mutate IY
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0xFDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x78u; memory[0x0003u] = 0x56u; // LD IY,0x5678
        memory[0x0004u] = 0xFDu; memory[0x0005u] = 0x66u; memory[0x0006u] = 0x01u; // LD H,(IY+1)

        memory[0x5679u] = 0xCDu; // IY+1

        CHECK_OR_FAIL(cpu.step() == 14u, "LD IY,nn setup failed");
        CHECK_OR_FAIL(cpu.step() == 19u, "FD66 LD H,(IY+d) should take 19 cycles");
        CHECK_OR_FAIL(cpu.IY == 0x5678u, "FD66 should not mutate IY");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.HL >> 8u) == 0xCDu, "FD66 should load value into H (HL high)");
    }

    // FD 7E: LD A,(IY+d) -- should load A and NOT mutate IY
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0xFDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x00u; memory[0x0003u] = 0x60u; // LD IY,0x6000
        memory[0x0004u] = 0xFDu; memory[0x0005u] = 0x7Eu; memory[0x0006u] = 0x03u; // LD A,(IY+3)

        memory[0x6003u] = 0x42u;

        CHECK_OR_FAIL(cpu.step() == 14u, "LD IY,nn setup failed");
        CHECK_OR_FAIL(cpu.step() == 19u, "FD7E LD A,(IY+d) should take 19 cycles");
        CHECK_OR_FAIL(cpu.IY == 0x6000u, "FD7E should not mutate IY");
        CHECK_OR_FAIL(static_cast<uint8_t>(cpu.AF >> 8u) == 0x42u, "FD7E should load value into A");
    }

    // FD 77: LD (IY+d),A -- should write memory and NOT mutate IY
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0xFDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x10u; memory[0x0003u] = 0x20u; // LD IY,0x2010
        memory[0x0004u] = 0x3Eu; memory[0x0005u] = 0x99u; // LD A,0x99
        memory[0x0006u] = 0xFDu; memory[0x0007u] = 0x77u; memory[0x0008u] = 0x05u; // LD (IY+5),A

        CHECK_OR_FAIL(cpu.step() == 14u, "LD IY,nn setup failed");
        CHECK_OR_FAIL(cpu.step() == 7u, "LD A,n setup failed");
        CHECK_OR_FAIL(cpu.step() == 19u, "FD77 LD (IY+d),A should take 19 cycles");
        CHECK_OR_FAIL(cpu.IY == 0x2010u, "FD77 should not mutate IY");
        CHECK_OR_FAIL(memory[0x2015u] == 0x99u, "FD77 should store A into memory at IY+d");
    }

    // FD 34: INC (IY+d) -- should increment memory and NOT mutate IY
    {
        std::vector<uint8_t> memory(0x10000u, 0x00u);
        auto cpu = makeCpu(memory);

        memory[0x0000u] = 0xFDu; memory[0x0001u] = 0x21u; memory[0x0002u] = 0x00u; memory[0x0003u] = 0x40u; // LD IY,0x4000
        memory[0x0004u] = 0xFDu; memory[0x0005u] = 0x34u; memory[0x0006u] = 0x03u; // INC (IY+3)

        memory[0x4003u] = 0x7Fu;

        CHECK_OR_FAIL(cpu.step() == 14u, "LD IY,nn setup failed");
        CHECK_OR_FAIL(cpu.step() == 23u, "FD34 INC (IY+d) should take 23 cycles");
        CHECK_OR_FAIL(cpu.IY == 0x4000u, "FD34 should not mutate IY");
        CHECK_OR_FAIL(memory[0x4003u] == 0x80u, "FD34 should increment the memory byte at IY+d");
    }

    return 0;
}
