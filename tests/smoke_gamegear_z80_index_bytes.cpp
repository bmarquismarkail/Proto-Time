#include "cores/gamegear/Z80Interpreter.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
Z80Interpreter makeCpu(std::vector<uint8_t>& memory, uint8_t prefix, uint8_t opcode) {
    memory[0] = prefix; memory[1] = opcode;
    Z80Interpreter cpu;
    cpu.setMemoryInterface([&](uint16_t address) { return memory[address]; },
                           [&](uint16_t address, uint8_t value) { memory[address] = value; });
    cpu.reset(); cpu.HL = 0xABCD; cpu.IX = 0x1234; cpu.IY = 0x5678; cpu.AF = 0x0155;
    return cpu;
}
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)
}

int main() {
    for (uint8_t prefix : {uint8_t{0xDD}, uint8_t{0xFD}}) {
        const uint16_t initialIndex = prefix == 0xDD ? 0x1234 : 0x5678;
        const uint8_t high = static_cast<uint8_t>(initialIndex >> 8);
        const uint8_t low = static_cast<uint8_t>(initialIndex);
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x44); // LD B,IXH/IYH
            CHECK(cpu.step() == 8, "index byte register load cycles");
            CHECK((cpu.BC >> 8) == high && cpu.HL == 0xABCD && cpu.AF == 0x0155,
                  "register-only prefixed load read H instead of index high byte");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x6F); // LD IXL/IYL,A
            cpu.AF = 0xE155;
            CHECK(cpu.step() == 8, "index byte register store cycles");
            CHECK((prefix == 0xDD ? cpu.IX : cpu.IY) == ((initialIndex & 0xFF00) | 0xE1)
                  && cpu.HL == 0xABCD && cpu.AF == 0xE155,
                  "register-only prefixed store wrote L instead of index low byte");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x65); // LD IXH,IXL / LD IYH,IYL
            CHECK(cpu.step() == 8, "index byte cross-copy cycles");
            CHECK((prefix == 0xDD ? cpu.IX : cpu.IY) == uint16_t((low << 8) | low)
                  && cpu.HL == 0xABCD, "index high/low cross-copy used ordinary HL");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x84); // ADD A,IXH/IYH
            CHECK(cpu.step() == 8 && (cpu.AF >> 8) == uint8_t(1 + high)
                  && cpu.HL == 0xABCD, "prefixed ALU did not use index high byte");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0xAD); // XOR IXL/IYL
            cpu.AF = 0xF055;
            CHECK(cpu.step() == 8 && (cpu.AF >> 8) == uint8_t(0xF0 ^ low)
                  && cpu.HL == 0xABCD, "prefixed ALU did not use index low byte");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x66); // LD H,(IX/IY+1): ordinary H
            memory[2] = 1; memory[uint16_t(initialIndex + 1)] = 0xE7;
            CHECK(cpu.step() == 19 && cpu.HL == 0xE7CD
                  && (prefix == 0xDD ? cpu.IX : cpu.IY) == initialIndex,
                  "indexed memory load must preserve the index and write ordinary H");
        }
        {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0x75); // LD (IX/IY-1),L: ordinary L
            memory[2] = 0xFF;
            CHECK(cpu.step() == 19 && memory[uint16_t(initialIndex - 1)] == 0xCD
                  && cpu.HL == 0xABCD && (prefix == 0xDD ? cpu.IX : cpu.IY) == initialIndex,
                  "indexed memory store must read ordinary L");
        }
        for (uint8_t destination : {uint8_t{4}, uint8_t{5}}) {
            std::vector<uint8_t> memory(65536);
            auto cpu = makeCpu(memory, prefix, 0xCB);
            memory[2] = 1; memory[3] = destination; // RLC (IX/IY+1),H/L
            memory[uint16_t(initialIndex + 1)] = 0x81;
            const auto expectedHL = destination == 4 ? 0x03CD : 0xAB03;
            CHECK(cpu.step() == 23 && cpu.HL == expectedHL
                  && memory[uint16_t(initialIndex + 1)] == 3
                  && (prefix == 0xDD ? cpu.IX : cpu.IY) == initialIndex,
                  "DD/FD-CB destination must remain ordinary H/L");
        }
    }
    return 0;
}
