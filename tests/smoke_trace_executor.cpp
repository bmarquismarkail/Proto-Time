#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

namespace {

std::vector<uint8_t> makeTraceRom()
{
    std::vector<uint8_t> rom(0x8000, 0x00);
    static constexpr std::array<uint8_t, 20> kProgram{{
        0x3E, 0x12,       // ld a,$12
        0x06, 0x34,       // ld b,$34
        0x80,             // add a,b
        0x20, 0x02,       // jr nz,+2
        0x3E, 0x00,       // (skipped when branch is taken)
        0xEA, 0x00, 0xC0, // ld ($c000),a
        0x21, 0x00, 0xC1, // ld hl,$c100
        0x36, 0x99,       // ld (hl),$99
        0xCB, 0x7C,       // bit 7,h
        0x00,             // nop
    }};

    for (std::size_t i = 0; i < kProgram.size(); ++i) {
        rom[0x0100 + i] = kProgram[i];
    }
    return rom;
}

void assertVisibleStateEqual(const GameBoyMachine& lhs, const GameBoyMachine& rhs)
{
    const auto& left = lhs.runtimeContext();
    const auto& right = rhs.runtimeContext();

    for (const std::string_view reg : {
             GB::RegisterId::AF,
             GB::RegisterId::BC,
             GB::RegisterId::DE,
             GB::RegisterId::HL,
             GB::RegisterId::SP,
             GB::RegisterId::PC,
         }) {
        assert(left.readRegister16(reg) == right.readRegister16(reg));
    }

    for (const uint16_t address : {
             static_cast<uint16_t>(0xC000u),
             static_cast<uint16_t>(0xC100u),
             static_cast<uint16_t>(0xFF0Fu),
             static_cast<uint16_t>(0xFF40u),
             static_cast<uint16_t>(0xFF44u),
         }) {
        assert(left.read8(address) == right.read8(address));
    }
}

} // namespace

int main()
{
    GameBoyMachine baselineMachine;
    GameBoyMachine optimizedMachine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;

    const auto rom = makeTraceRom();
    baselineMachine.loadRom(rom);
    optimizedMachine.loadRom(rom);
    optimizedMachine.attachExecutorPolicy(optimizedPolicy);

    assert(baselineMachine.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(optimizedMachine.guarantee() == BMMQ::ExecutionGuarantee::VisibleStatePreserving);

    bool sawFastPath = false;
    for (int stepIndex = 0; stepIndex < 9; ++stepIndex) {
        baselineMachine.step();
        optimizedMachine.step();

        const auto& baselineFeedback = baselineMachine.runtimeContext().getLastFeedback();
        const auto& optimizedFeedback = optimizedMachine.runtimeContext().getLastFeedback();

        assert(baselineFeedback.executionPath == BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute);
        assert(optimizedFeedback.executionPath != BMMQ::ExecutionPathHint::Unknown);
        sawFastPath = sawFastPath ||
            optimizedFeedback.executionPath == BMMQ::ExecutionPathHint::CpuOptimizedFastPath;

        assertVisibleStateEqual(baselineMachine, optimizedMachine);
    }

    assert(sawFastPath);
    assert(baselineMachine.runtimeContext().read8(0xC000) == 0x46u);
    assert(baselineMachine.runtimeContext().read8(0xC100) == 0x99u);
    assert(optimizedMachine.runtimeContext().read8(0xC000) == 0x46u);
    assert(optimizedMachine.runtimeContext().read8(0xC100) == 0x99u);

    return 0;
}
