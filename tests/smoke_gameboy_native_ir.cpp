#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/GameBoyNativeExecution.hpp"
#include "inst_cycle/IntermediateRepresentation.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {

struct AbiState {
    std::array<std::uint64_t, 14> registers{};
    std::array<std::uint8_t, 65536> memory{};
};

GB::IRExecution::ExecutionAbiV1 makeAbi(AbiState& state)
{
    return {
        .opaque = &state,
        .readRegister = [](void* opaque, GB::IRExecution::Register reg) {
            return static_cast<AbiState*>(opaque)->registers[static_cast<std::size_t>(reg)];
        },
        .writeRegister = [](void* opaque, GB::IRExecution::Register reg, std::uint64_t value) {
            static_cast<AbiState*>(opaque)->registers[static_cast<std::size_t>(reg)] = value;
        },
        .readMemory8 = [](void* opaque, std::uint16_t address) {
            return static_cast<AbiState*>(opaque)->memory[address];
        },
        .writeMemory8 = [](void* opaque, std::uint16_t address, std::uint8_t value) {
            static_cast<AbiState*>(opaque)->memory[address] = value;
        },
        .callHelper = [](void*, GB::IRExecution::Helper, const std::uint64_t*, std::size_t) {
            return std::uint64_t{0};
        },
        .retireCpuCycles = [](void*, std::uint32_t) {},
        .executionState = [](void*) { return std::uint64_t{0}; },
    };
}

void testArtifactIsBoundedAndWx()
{
    if (!GB::NativeExecution::supported()) return;
    const std::array translated{
        BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>{
            .address = 0x0100u, .bytes = {0x00u, 0u, 0u}, .length = 1u},
    };
    const auto ir = GB::IRExecution::lowerBlock(translated, 0u, 0u);
    assert(ir);
    std::string error;
    const auto artifact = GB::NativeExecution::compile(*ir, &error);
    assert(artifact && error.empty());
    assert(artifact->sealedExecutable());
    assert(artifact->instructionCount() == 1u);
    assert(artifact->codeSize() == 25u);
    assert(artifact->codeAddress() != nullptr);

#if defined(__linux__)
    std::ifstream maps("/proc/self/maps");
    assert(maps);
    const auto address = reinterpret_cast<std::uintptr_t>(artifact->codeAddress());
    bool found = false;
    std::string line;
    while (std::getline(maps, line)) {
        std::uintptr_t start = 0u;
        std::uintptr_t end = 0u;
        char dash = 0;
        std::string permissions;
        std::istringstream input(line);
        input >> std::hex >> start >> dash >> end >> permissions;
        if (dash == '-' && start <= address && address < end) {
            found = true;
            assert(permissions.size() >= 3u);
            assert(permissions[0] == 'r');
            assert(permissions[1] != 'w');
            assert(permissions[2] == 'x');
            break;
        }
    }
    assert(found);
#endif

    AbiState state;
    auto abi = makeAbi(state);
    const auto result = artifact->execute(0u, abi);
    assert(result.retirementReached);
    assert(!result.exitRequested);
    assert(state.registers[static_cast<std::size_t>(GB::IRExecution::Register::PC)] == 0x0101u);

    auto oversized = *ir;
    auto& operations = oversized.instructions[0].operations;
    const auto retire = operations.back();
    operations.pop_back();
    while (operations.size() <= 64u) {
        BMMQ::IR::Operation constant;
        constant.opcode = BMMQ::IR::Opcode::Constant;
        constant.result = static_cast<BMMQ::IR::ValueId>(operations.size());
        constant.resultType = BMMQ::IR::ValueType::I8;
        constant.operands.push_back(BMMQ::IR::Operand::immediate(0u, BMMQ::IR::ValueType::I8));
        operations.push_back(std::move(constant));
    }
    operations.push_back(retire);
    error.clear();
    assert(!GB::NativeExecution::compile(oversized, &error));
    assert(!error.empty());
}

std::vector<std::uint8_t> testRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x06u; rom[0x0101u] = 0x03u;
    rom[0x0102u] = 0x04u;
    rom[0x0103u] = 0x80u;
    rom[0x0104u] = 0x77u;
    rom[0x0105u] = 0x18u; rom[0x0106u] = 0xF9u;
    return rom;
}

void assertEquivalent(const GB::GameBoyMachine& lhs, const GB::GameBoyMachine& rhs)
{
    for (const auto* reg : {"AF", "BC", "DE", "HL", "SP", "PC"}) {
        assert(lhs.runtimeContext().readRegister16(reg) == rhs.runtimeContext().readRegister16(reg));
    }
    assert(lhs.runtimeContext().peek8(0xC100u) == rhs.runtimeContext().peek8(0xC100u));
    assert(lhs.runtimeContext().getLastFeedback().retiredCycles ==
           rhs.runtimeContext().getLastFeedback().retiredCycles);
}

void testNativeMachineMatchesBaselineAndInvalidates()
{
    if (!GB::NativeExecution::supported()) return;
    GB::GameBoyMachine baseline;
    GB::GameBoyMachine native;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    baseline.setBlockCacheEnabled(false);
    native.attachExecutorPolicy(policy);
    native.setBlockCacheEnabled(true);
    native.setNativeIrEnabled(true);
    assert(native.nativeIrEnabled());
    baseline.loadRom(testRom());
    native.loadRom(testRom());
    baseline.runtimeContext().writeRegister16(GB::RegisterId::HL, 0xC100u);
    native.runtimeContext().writeRegister16(GB::RegisterId::HL, 0xC100u);

    bool sawNative = false;
    for (std::size_t step = 0u; step < 128u; ++step) {
        baseline.step();
        native.step();
        sawNative = sawNative || native.runtimeContext().getLastFeedback().executionPath ==
            BMMQ::ExecutionPathHint::NativeIr;
        assertEquivalent(baseline, native);
    }
    assert(sawNative);
    const auto invalidations = native.blockCacheStats().invalidations.load();
    native.runtimeContext().write8(0x0100u, 0x55u);
    assert(native.blockCacheStats().invalidations.load() > invalidations);
}

} // namespace

int main()
{
    testArtifactIsBoundedAndWx();
    testNativeMachineMatchesBaselineAndInvalidates();
}
