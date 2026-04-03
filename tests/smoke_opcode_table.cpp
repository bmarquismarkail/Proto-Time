#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/gameboy.hpp"
#include "machine/RegisterId.hpp"

namespace {

using RegisterPair = BMMQ::CPU_RegisterPair<uint16_t>;

void step(LR3592_DMG& cpu)
{
    auto fetchBlock = cpu.fetch();
    auto execBlock = cpu.decode(fetchBlock);
    cpu.execute(execBlock, fetchBlock);
}

RegisterPair* pair(LR3592_DMG& cpu, BMMQ::RegisterId id)
{
    auto* entry = cpu.getMemory().file.findRegister(id);
    assert(entry != nullptr);
    assert(entry->reg != nullptr);
    auto* reg = dynamic_cast<RegisterPair*>(entry->reg.get());
    assert(reg != nullptr);
    return reg;
}

uint16_t scalar(LR3592_DMG& cpu, BMMQ::RegisterId id)
{
    auto* entry = cpu.getMemory().file.findRegister(id);
    assert(entry != nullptr);
    assert(entry->reg != nullptr);
    return entry->reg->value;
}

void scalar(LR3592_DMG& cpu, BMMQ::RegisterId id, uint16_t value)
{
    auto* entry = cpu.getMemory().file.findRegister(id);
    assert(entry != nullptr);
    assert(entry->reg != nullptr);
    entry->reg->value = value;
}

uint8_t readByte(LR3592_DMG& cpu, uint16_t address)
{
    uint8_t value = 0;
    cpu.getMemory().read(&value, address, 1);
    return value;
}

void requireDecodeFailure(const std::vector<uint8_t>& program)
{
    LR3592_DMG cpu;
    cpu.loadProgram(program);
    auto fetchBlock = cpu.fetch();

    bool threw = false;
    try {
        (void)cpu.decode(fetchBlock);
    } catch (const std::exception&) {
        threw = true;
    } catch (...) {
        threw = true;
    }

    assert(threw);
}

void loadAndSetPc(LR3592_DMG& cpu, uint16_t address, const std::vector<uint8_t>& program)
{
    cpu.loadProgram(program, address);
    scalar(cpu, GB::RegisterId::PC, address);
}

}

int main()
{
    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x00, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x00);
        assert(scalar(cpu, GB::RegisterId::PC) == 1);
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x01, 0x34, 0x12});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::BC)->value == 0x1234);
        assert(scalar(cpu, GB::RegisterId::PC) == 3);
    }

    {
        LR3592_DMG cpu;
        loadAndSetPc(cpu, 0x0000, {0x3E, 0x12, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x12);
        assert(scalar(cpu, GB::RegisterId::PC) == 2);

        loadAndSetPc(cpu, 0x0004, {0x06, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::BC)->hi == 0x00);
    }

    {
        LR3592_DMG cpu;
        loadAndSetPc(cpu, 0x0000, {0x06, 0x34, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::BC)->hi == 0x34);

        loadAndSetPc(cpu, 0x0004, {0x78, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x34);
    }

    {
        LR3592_DMG cpu;
        loadAndSetPc(cpu, 0x0000, {0x3E, 0x10, 0x00});
        step(cpu);
        loadAndSetPc(cpu, 0x0004, {0xC6, 0x05, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x15);
    }

    {
        LR3592_DMG cpu;
        loadAndSetPc(cpu, 0x0000, {0x3E, 0x10, 0x00});
        step(cpu);
        loadAndSetPc(cpu, 0x0004, {0x06, 0x05, 0x00});
        step(cpu);
        cpu.loadProgram({0x80, 0x00, 0x00}, 4);
        scalar(cpu, GB::RegisterId::PC, 4);
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x15);
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x18, 0x02, 0x00}, 0);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 4);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::AF)->lo = 0x80;
        cpu.loadProgram({0x28, 0x02, 0x00}, 0);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 4);
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0xC3, 0x34, 0x12}, 0);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x1234);
    }

    {
        LR3592_DMG cpu;
        scalar(cpu, GB::RegisterId::SP, 0xC100);
        cpu.loadProgram({0xCD, 0x06, 0x00}, 0);
        cpu.loadProgram({0xC9, 0x00, 0x00}, 6);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0006);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC0FE);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0003);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC100);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::BC)->value = 0x1234;
        scalar(cpu, GB::RegisterId::SP, 0xC100);
        loadAndSetPc(cpu, 0x0000, {0xC5, 0x00, 0x00});
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC0FE);

        loadAndSetPc(cpu, 0x0004, {0xD1, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::DE)->value == 0x1234);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC100);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::AF)->hi = 0x42;
        loadAndSetPc(cpu, 0x0000, {0xE0, 0x10, 0x00});
        step(cpu);
        assert(readByte(cpu, 0xFF10) == 0x42);

        loadAndSetPc(cpu, 0x0004, {0xF0, 0x10, 0x00});
        pair(cpu, GB::RegisterId::AF)->hi = 0x00; // clear A before loading back
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x42);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::AF)->hi = 0x77;
        cpu.loadProgram({0xEA, 0x00, 0xC0}, 0);
        step(cpu);
        assert(readByte(cpu, 0xC000) == 0x77);

        cpu.loadProgram({0xFA, 0x00, 0xC0}, 4);
        pair(cpu, GB::RegisterId::AF)->hi = 0x00;
        scalar(cpu, GB::RegisterId::PC, 4);
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x77);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::HL)->value = 0xC000;
        loadAndSetPc(cpu, 0x0000, {0x22, 0x00, 0x00});
        pair(cpu, GB::RegisterId::AF)->hi = 0x10;
        step(cpu);
        assert(readByte(cpu, 0xC000) == 0x10);
        assert(pair(cpu, GB::RegisterId::HL)->value == 0xC001);

        pair(cpu, GB::RegisterId::AF)->hi = 0x00;
        cpu.getMemory().store.load(std::span<const uint8_t>({0x33}), static_cast<uint16_t>(0xC001));
        loadAndSetPc(cpu, 0x0004, {0x2A, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x33);
        assert(pair(cpu, GB::RegisterId::HL)->value == 0xC002);

        pair(cpu, GB::RegisterId::AF)->hi = 0x55;
        loadAndSetPc(cpu, 0x0008, {0x32, 0x00, 0x00});
        step(cpu);
        assert(readByte(cpu, 0xC002) == 0x55);
        assert(pair(cpu, GB::RegisterId::HL)->value == 0xC001);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::HL)->value = 0xC010;
        cpu.getMemory().store.load(std::span<const uint8_t>({0x05}), static_cast<uint16_t>(0xC010));
        loadAndSetPc(cpu, 0x0000, {0x34, 0x00, 0x00});
        step(cpu);
        assert(readByte(cpu, 0xC010) == 0x06);
        loadAndSetPc(cpu, 0x0004, {0x35, 0x00, 0x00});
        step(cpu);
        assert(readByte(cpu, 0xC010) == 0x05);
    }

    {
        LR3592_DMG cpu;
        scalar(cpu, GB::RegisterId::SP, 0xFFF0);
        loadAndSetPc(cpu, 0x0000, {0xE8, 0x04, 0x00});
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xFFF4);

        loadAndSetPc(cpu, 0x0004, {0xF8, 0x00, 0x00});
        step(cpu);
        assert(pair(cpu, GB::RegisterId::HL)->value == 0xFFF4);

        pair(cpu, GB::RegisterId::HL)->value = 0xC222;
        loadAndSetPc(cpu, 0x0008, {0xF9, 0x00, 0x00});
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC222);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::BC)->hi = 0x00;
        cpu.loadProgram({0xCB, 0xC0, 0x00}, 0);
        step(cpu);
        assert(pair(cpu, GB::RegisterId::BC)->hi == 0x01);
    }

    {
        LR3592_DMG cpu;
        scalar(cpu, GB::RegisterId::SP, 0xC100);
        cpu.getMemory().store.load(std::span<const uint8_t>({0x03, 0x00}), static_cast<uint16_t>(0xC0FE));
        scalar(cpu, GB::RegisterId::SP, 0xC0FE);
        cpu.loadProgram({0xD9, 0x00, 0x00}, 0);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0003);
        auto* imeEntry = cpu.getMemory().file.findRegister("ime");
        assert(imeEntry != nullptr);
        assert(imeEntry->reg != nullptr);
        assert(imeEntry->reg->value == 1);
    }

    requireDecodeFailure({0xD3, 0x00, 0x00});

    return 0;
}
