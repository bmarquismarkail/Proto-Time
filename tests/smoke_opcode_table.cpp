#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/gameboy.hpp"
#include "machine/RegisterId.hpp"

namespace {

using RegisterPair = BMMQ::CPU_RegisterPair<uint16_t>;

BMMQ::CpuFeedback step(LR3592_DMG& cpu)
{
    auto fetchBlock = cpu.fetch();
    auto execBlock = cpu.decode(fetchBlock);
    cpu.execute(execBlock, fetchBlock);
    return cpu.getLastFeedback();
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
        if (cpu.clockHz() != 4194304u) {
            throw std::runtime_error("LR3592 clock contract should expose 4194304 Hz");
        }
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x00, 0x00, 0x00});
        auto fetchBlock = cpu.fetch();
        auto execBlock = cpu.decode(fetchBlock);
        if (execBlock.cyclesIfNotTaken() != 4 || execBlock.cyclesIfTaken() != 4) {
            throw std::runtime_error("NOP should decode to 4 cycles");
        }
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x20, 0x02, 0x00});
        auto fetchBlock = cpu.fetch();
        auto execBlock = cpu.decode(fetchBlock);
        if (execBlock.cyclesIfNotTaken() != 8 || execBlock.cyclesIfTaken() != 12) {
            throw std::runtime_error("JR NZ should decode to 8/12 cycles");
        }
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::HL)->value = 0xC000;
        cpu.loadProgram({0xCB, 0x06, 0x00});
        auto fetchBlock = cpu.fetch();
        auto execBlock = cpu.decode(fetchBlock);
        if (execBlock.cyclesIfNotTaken() != 16 || execBlock.cyclesIfTaken() != 16) {
            throw std::runtime_error("RLC (HL) should decode to 16 cycles");
        }
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x00, 0x00, 0x00});
        const auto feedback = step(cpu);
        if (feedback.retiredCycles != 4u) {
            throw std::runtime_error("NOP should retire 4 cycles");
        }
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
        const auto feedback = step(cpu);
        if (feedback.retiredCycles != 12u) {
            throw std::runtime_error("JR should retire 12 cycles when taken");
        }
        assert(scalar(cpu, GB::RegisterId::PC) == 4);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::AF)->lo = 0x80;
        cpu.loadProgram({0x28, 0x02, 0x00}, 0);
        const auto feedback = step(cpu);
        if (feedback.retiredCycles != 12u) {
            throw std::runtime_error("JR Z should retire 12 cycles when taken");
        }
        assert(scalar(cpu, GB::RegisterId::PC) == 4);
    }

    {
        LR3592_DMG cpu;
        pair(cpu, GB::RegisterId::AF)->lo = 0x00;
        cpu.loadProgram({0x28, 0x02, 0x00}, 0);
        const auto feedback = step(cpu);
        if (feedback.retiredCycles != 8u) {
            throw std::runtime_error("JR Z should retire 8 cycles when not taken");
        }
        assert(scalar(cpu, GB::RegisterId::PC) == 2);
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
        auto* timaEntry = cpu.getMemory().file.findRegister("TIMA");
        auto* tmaEntry = cpu.getMemory().file.findRegister("TMA");
        auto* tacEntry = cpu.getMemory().file.findRegister("TAC");
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(timaEntry != nullptr && timaEntry->reg != nullptr);
        assert(tmaEntry != nullptr && tmaEntry->reg != nullptr);
        assert(tacEntry != nullptr && tacEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        timaEntry->reg->value = 0xFF;
        tmaEntry->reg->value = 0xAB;
        tacEntry->reg->value = 0x05;
        ifEntry->reg->value = 0x00;

        cpu.loadProgram({0x00, 0x00, 0x00, 0x00, 0x00}, 0);
        for (int i = 0; i < 4; ++i) {
            step(cpu);
        }

        assert(static_cast<uint8_t>(timaEntry->reg->value) == 0xAB);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x04u) != 0);
    }

    {
        LR3592_DMG cpu;
        scalar(cpu, GB::RegisterId::SP, 0xC100);
        auto* ieEntry = cpu.getMemory().file.findRegister(GB::RegisterId::IE);
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(ieEntry != nullptr && ieEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        ieEntry->reg->value = 0x04;
        ifEntry->reg->value = 0x04;
        cpu.loadProgram({0xFB, 0x00, 0x00}, 0);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0001);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC100);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0002);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0050);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC0FE);
        assert(readByte(cpu, 0xC0FE) == 0x02);
        assert(readByte(cpu, 0xC0FF) == 0x00);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x04u) == 0);
    }

    {
        LR3592_DMG cpu;
        auto* lcdcEntry = cpu.getMemory().file.findRegister("LCDC");
        auto* lyEntry = cpu.getMemory().file.findRegister("LY");
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(lcdcEntry != nullptr && lcdcEntry->reg != nullptr);
        assert(lyEntry != nullptr && lyEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        lcdcEntry->reg->value = 0x80;
        ifEntry->reg->value = 0x00;
        cpu.loadProgram(std::vector<uint8_t>(16416, 0x00), 0);

        for (int i = 0; i < 114; ++i) {
            step(cpu);
        }
        assert(static_cast<uint8_t>(lyEntry->reg->value) == 1);

        for (int i = 0; i < 143 * 114; ++i) {
            step(cpu);
        }
        assert(static_cast<uint8_t>(lyEntry->reg->value) == 144);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x01u) != 0);
    }

    {
        LR3592_DMG cpu;
        scalar(cpu, GB::RegisterId::SP, 0xC100);
        auto* lcdcEntry = cpu.getMemory().file.findRegister("LCDC");
        auto* statEntry = cpu.getMemory().file.findRegister("STAT");
        auto* ieEntry = cpu.getMemory().file.findRegister(GB::RegisterId::IE);
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(lcdcEntry != nullptr && lcdcEntry->reg != nullptr);
        assert(statEntry != nullptr && statEntry->reg != nullptr);
        assert(ieEntry != nullptr && ieEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        lcdcEntry->reg->value = 0x80;
        statEntry->reg->value = 0x20;
        ieEntry->reg->value = 0x02;
        ifEntry->reg->value = 0x00;
        cpu.loadProgram({0xFB, 0x00, 0x00, 0x00}, 0);

        step(cpu);
        step(cpu);
        step(cpu);

        assert(scalar(cpu, GB::RegisterId::PC) == 0x0048);
        assert(scalar(cpu, GB::RegisterId::SP) == 0xC0FE);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x02u) == 0);
    }

    {
        LR3592_DMG cpu;
        auto* lcdcEntry = cpu.getMemory().file.findRegister("LCDC");
        assert(lcdcEntry != nullptr && lcdcEntry->reg != nullptr);

        lcdcEntry->reg->value = 0x80;
        cpu.loadProgram(std::vector<uint8_t>(512, 0x00), 0);
        step(cpu);

        cpu.getMemory().write(std::span<const uint8_t>({0x12}), static_cast<uint16_t>(0xFE00));
        assert(readByte(cpu, 0xFE00) == 0xFF);

        for (int i = 0; i < 70; ++i) {
            step(cpu);
        }

        cpu.getMemory().write(std::span<const uint8_t>({0x34}), static_cast<uint16_t>(0xFE00));
        assert(readByte(cpu, 0xFE00) == 0x34);
    }

    {
        LR3592_DMG cpu;
        auto* lcdcEntry = cpu.getMemory().file.findRegister("LCDC");
        assert(lcdcEntry != nullptr && lcdcEntry->reg != nullptr);

        lcdcEntry->reg->value = 0x80;
        cpu.loadProgram(std::vector<uint8_t>(512, 0x00), 0);
        for (int i = 0; i < 20; ++i) {
            step(cpu);
        }

        cpu.getMemory().write(std::span<const uint8_t>({0x56}), static_cast<uint16_t>(0x8000));
        assert(readByte(cpu, 0x8000) == 0xFF);

        for (int i = 0; i < 50; ++i) {
            step(cpu);
        }

        cpu.getMemory().write(std::span<const uint8_t>({0x78}), static_cast<uint16_t>(0x8000));
        assert(readByte(cpu, 0x8000) == 0x78);
    }

    {
        LR3592_DMG cpu;
        std::vector<uint8_t> source(0xA0, 0x00);
        for (std::size_t i = 0; i < source.size(); ++i) {
            source[i] = static_cast<uint8_t>(i ^ 0x5A);
        }
        cpu.getMemory().store.load(std::span<const uint8_t>(source.data(), source.size()), static_cast<uint16_t>(0xC000));
        cpu.getMemory().store.load(std::span<const uint8_t>({0x99}), static_cast<uint16_t>(0xFF80));
        cpu.loadProgram(std::vector<uint8_t>(512, 0x00), 0);

        cpu.getMemory().write(std::span<const uint8_t>({0xC0}), static_cast<uint16_t>(0xFF46));

        assert(readByte(cpu, 0xC000) == 0xFF);
        assert(readByte(cpu, 0xFE00) == 0xFF);
        assert(readByte(cpu, 0xFF80) == 0x99);

        const auto stalledPc = scalar(cpu, GB::RegisterId::PC);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == stalledPc);

        for (int i = 1; i < 160; ++i) {
            step(cpu);
        }

        assert(scalar(cpu, GB::RegisterId::PC) == stalledPc);
        assert(readByte(cpu, 0xC000) == source[0]);
        for (std::size_t i = 0; i < source.size(); ++i) {
            assert(readByte(cpu, static_cast<uint16_t>(0xFE00 + i)) == source[i]);
        }

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == static_cast<uint16_t>(stalledPc + 1));
    }

    {
        LR3592_DMG cpu;
        auto* dmaEntry = cpu.getMemory().file.findRegister("DMA");
        assert(dmaEntry != nullptr && dmaEntry->reg != nullptr);

        cpu.getMemory().store.load(std::span<const uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}), static_cast<uint16_t>(0x8000));
        cpu.getMemory().write(std::span<const uint8_t>({0x80}), static_cast<uint16_t>(0xFF46));
        assert(static_cast<uint8_t>(dmaEntry->reg->value) == 0x80);

        for (int i = 0; i < 160; ++i) {
            step(cpu);
        }

        assert(readByte(cpu, 0xFE00) == 0xDE);
        assert(readByte(cpu, 0xFE01) == 0xAD);
        assert(readByte(cpu, 0xFE02) == 0xBE);
        assert(readByte(cpu, 0xFE03) == 0xEF);
    }

    {
        LR3592_DMG cpu;
        auto* divEntry = cpu.getMemory().file.findRegister("DIV");
        auto* timaEntry = cpu.getMemory().file.findRegister("TIMA");
        auto* statEntry = cpu.getMemory().file.findRegister("STAT");
        auto* lyEntry = cpu.getMemory().file.findRegister("LY");
        auto* ieEntry = cpu.getMemory().file.findRegister(GB::RegisterId::IE);
        assert(divEntry != nullptr && divEntry->reg != nullptr);
        assert(timaEntry != nullptr && timaEntry->reg != nullptr);
        assert(statEntry != nullptr && statEntry->reg != nullptr);
        assert(lyEntry != nullptr && lyEntry->reg != nullptr);
        assert(ieEntry != nullptr && ieEntry->reg != nullptr);

        divEntry->reg->value = 0x9Au;
        timaEntry->reg->value = 0xABu;
        statEntry->reg->value = 0x85u;
        lyEntry->reg->value = 0x22u;
        ieEntry->reg->value = 0x1Fu;

        assert(readByte(cpu, 0xFF04) == 0x9Au);
        assert(readByte(cpu, 0xFF05) == 0xABu);
        assert(readByte(cpu, 0xFF41) == 0x85u);
        assert(readByte(cpu, 0xFF44) == 0x22u);
        assert(readByte(cpu, 0xFFFF) == 0x1Fu);
    }

    {
        LR3592_DMG cpu;
        auto* joypEntry = cpu.getMemory().file.findRegister("JOYP");
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(joypEntry != nullptr && joypEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        cpu.setJoypadState(0);
        cpu.getMemory().write(std::span<const uint8_t>({0x20}), static_cast<uint16_t>(0xFF00));
        assert((readByte(cpu, 0xFF00) & 0x30u) == 0x20u);
        assert((readByte(cpu, 0xFF00) & 0x0Fu) == 0x0Fu);

        cpu.setJoypadState(GB::Joypad::Right | GB::Joypad::Start);
        assert((readByte(cpu, 0xFF00) & 0x0Fu) == 0x0Eu);

        cpu.getMemory().write(std::span<const uint8_t>({0x10}), static_cast<uint16_t>(0xFF00));
        assert((readByte(cpu, 0xFF00) & 0x30u) == 0x10u);
        assert((readByte(cpu, 0xFF00) & 0x0Fu) == 0x07u);

        ifEntry->reg->value = 0x00;
        cpu.setJoypadState(GB::Joypad::Right | GB::Joypad::Start);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x10u) == 0);

        cpu.setJoypadState(static_cast<uint8_t>(GB::Joypad::Right | GB::Joypad::B | GB::Joypad::Start));
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x10u) != 0);
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x10, 0x00, 0x00}, 0);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0002);
        auto stoppedFetch = cpu.fetch();
        assert(!stoppedFetch.getblockData().empty());
        assert(stoppedFetch.getblockData().front().data.empty());

        cpu.setJoypadState(GB::Joypad::Start);
        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0003);
    }

    {
        LR3592_DMG cpu;
        auto* lyEntry = cpu.getMemory().file.findRegister("LY");
        auto* statEntry = cpu.getMemory().file.findRegister("STAT");
        assert(lyEntry != nullptr && lyEntry->reg != nullptr);
        assert(statEntry != nullptr && statEntry->reg != nullptr);

        lyEntry->reg->value = 0x25;
        statEntry->reg->value = 0x83;

        cpu.getMemory().write(std::span<const uint8_t>({0x91}), static_cast<uint16_t>(0xFF44));
        const auto statLowBitsBeforeWrite = static_cast<uint8_t>(statEntry->reg->value & 0x07u);
        cpu.getMemory().write(std::span<const uint8_t>({0x78}), static_cast<uint16_t>(0xFF41));

        assert(static_cast<uint8_t>(lyEntry->reg->value) == 0x00);
        assert(readByte(cpu, 0xFF44) == 0x00);
        assert((static_cast<uint8_t>(statEntry->reg->value) & 0x78u) == 0x78u);
        assert((static_cast<uint8_t>(statEntry->reg->value) & 0x07u) == statLowBitsBeforeWrite);
    }

    {
        LR3592_DMG cpu;
        auto* sbEntry = cpu.getMemory().file.findRegister("SB");
        auto* scEntry = cpu.getMemory().file.findRegister("SC");
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(sbEntry != nullptr && sbEntry->reg != nullptr);
        assert(scEntry != nullptr && scEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        sbEntry->reg->value = 0xA5;
        ifEntry->reg->value = 0x00;
        cpu.loadProgram(std::vector<uint8_t>(300, 0x00), 0);
        cpu.getMemory().write(std::span<const uint8_t>({0x81}), static_cast<uint16_t>(0xFF02));

        assert(static_cast<uint8_t>(sbEntry->reg->value) == 0xA5);
        assert((static_cast<uint8_t>(scEntry->reg->value) & 0x80u) != 0);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x08u) == 0);

        for (int i = 0; i < 1023; ++i) {
            step(cpu);
        }
        assert(static_cast<uint8_t>(sbEntry->reg->value) == 0xA5);
        assert((static_cast<uint8_t>(scEntry->reg->value) & 0x80u) != 0);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x08u) == 0);

        step(cpu);
        assert(static_cast<uint8_t>(sbEntry->reg->value) == 0xFF);
        assert((static_cast<uint8_t>(scEntry->reg->value) & 0x80u) == 0);
        assert((static_cast<uint8_t>(ifEntry->reg->value) & 0x08u) != 0);
    }

    {
        LR3592_DMG cpu;
        auto* ieEntry = cpu.getMemory().file.findRegister(GB::RegisterId::IE);
        auto* ifEntry = cpu.getMemory().file.findRegister("IF");
        assert(ieEntry != nullptr && ieEntry->reg != nullptr);
        assert(ifEntry != nullptr && ifEntry->reg != nullptr);

        ieEntry->reg->value = 0x01;
        ifEntry->reg->value = 0x01;
        cpu.setIme(false);
        cpu.loadProgram({0x76, 0x3E, 0x14, 0x00}, 0);

        step(cpu);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0001);

        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x3E);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0002);

        step(cpu);
        assert(pair(cpu, GB::RegisterId::AF)->hi == 0x3E);
        assert(pair(cpu, GB::RegisterId::DE)->hi == 0x01);
        assert(scalar(cpu, GB::RegisterId::PC) == 0x0003);
    }

    {
        LR3592_DMG cpu;
        auto* lcdcEntry = cpu.getMemory().file.findRegister("LCDC");
        auto* statEntry = cpu.getMemory().file.findRegister("STAT");
        assert(lcdcEntry != nullptr && lcdcEntry->reg != nullptr);
        assert(statEntry != nullptr && statEntry->reg != nullptr);

        lcdcEntry->reg->value = 0x80;
        cpu.loadProgram(std::vector<uint8_t>(256, 0x00), 0);

        step(cpu);
        assert((static_cast<uint8_t>(statEntry->reg->value) & 0x03u) == 0x02u);

        for (int i = 0; i < 20; ++i) {
            step(cpu);
        }
        assert((static_cast<uint8_t>(statEntry->reg->value) & 0x03u) == 0x03u);

        for (int i = 0; i < 50; ++i) {
            step(cpu);
        }
        assert((static_cast<uint8_t>(statEntry->reg->value) & 0x03u) == 0x00u);
    }

    requireDecodeFailure({0xD3, 0x00, 0x00});

    return 0;
}
