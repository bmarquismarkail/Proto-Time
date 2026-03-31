#include <cassert>
#include <cstdint>
#include <stdexcept>

#include "cores/gameboy/gameboy.hpp"
#include "machine/RegisterId.hpp"

int main()
{
    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x00, 0x00, 0x00});

        auto fetchBlock = cpu.fetch();
        auto execBlock = cpu.decode(fetchBlock);
        cpu.execute(execBlock, fetchBlock);

        auto& mem = cpu.getMemory();
        auto* afEntry = mem.file.findRegister(BMMQ::RegisterId::AF);
        auto* pcEntry = mem.file.findRegister(BMMQ::RegisterId::PC);
        assert(afEntry != nullptr);
        assert(afEntry->reg != nullptr);
        assert(pcEntry != nullptr);
        assert(pcEntry->reg != nullptr);

        auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->reg.get());
        assert(af != nullptr);
        assert(af->hi == 0x00);
        assert(pcEntry->reg->value == 3);
    }

    {
        LR3592_DMG cpu;
        cpu.loadProgram({0x01, 0x00, 0x00});

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

    return 0;
}
