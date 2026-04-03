#include <cassert>
#include <cstdint>

#include "cores/gameboy/decode/gb_interpreter.hpp"

int main()
{
    LR3592_DMG cpu;
    LR3592_Interpreter_Decode decoder(&cpu);

    auto& mem = cpu.getMemory();
    auto* afEntry = mem.file.findRegister("AF");
    assert(afEntry != nullptr);
    assert(afEntry->reg != nullptr);

    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->reg.get());
    assert(af != nullptr);

    auto* pcEntry = mem.file.findRegister("PC");
    auto* spEntry = mem.file.findRegister("SP");
    assert(pcEntry != nullptr);
    assert(pcEntry->reg != nullptr);
    assert(spEntry != nullptr);
    assert(spEntry->reg != nullptr);

    af->hi = 0x10;
    af->lo = 0x00;
    pcEntry->reg->value = 0x0000;
    mem.store.load(std::span<const uint8_t>({0xC6, 0x05}), static_cast<uint16_t>(0x0000));

    BMMQ::MemorySnapshot<uint16_t, uint8_t, uint16_t> snapshot(mem.store);
    decoder.setSnapshot(&snapshot);

    decoder.math_i8(0xC6);

    auto* afSnapEntry = snapshot.file.findRegister("AF");
    assert(afSnapEntry != nullptr);
    assert(afSnapEntry->reg != nullptr);

    auto* afSnap = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afSnapEntry->reg.get());
    assert(afSnap != nullptr);
    assert(static_cast<uint8_t>(afSnap->hi) == 0x15);

    spEntry->reg->value = 0xC100;
    pcEntry->reg->value = 0x0000;
    mem.store.load(std::span<const uint8_t>({0x34, 0x12}), static_cast<uint16_t>(0xC100));

    BMMQ::MemorySnapshot<uint16_t, uint8_t, uint16_t> retSnapshot(mem.store);
    decoder.setSnapshot(&retSnapshot);
    decoder.ret();

    auto* pcSnapEntry = retSnapshot.file.findRegister("PC");
    auto* spSnapEntry = retSnapshot.file.findRegister("SP");
    assert(pcSnapEntry != nullptr);
    assert(pcSnapEntry->reg != nullptr);
    assert(spSnapEntry != nullptr);
    assert(spSnapEntry->reg != nullptr);
    assert(pcSnapEntry->reg->value == 0x1234);
    assert(spSnapEntry->reg->value == 0xC102);

    return 0;
}
