#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/modding/NativeMod.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Until final : BMMQ::InstructionRetirementSink {
    GB::GameBoyMachine& machine;
    std::uint16_t target;
    Until(GB::GameBoyMachine& m, std::uint16_t pc) : machine(m), target(pc) {}
    BMMQ::InstructionRetirementDecision retireInstruction(
        const BMMQ::CpuFeedback&, const BMMQ::ExecutionSliceProgress&) override {
        return machine.runtimeContext().readRegister16(GB::RegisterId::PC) == target
            ? BMMQ::InstructionRetirementDecision::exitSlice()
            : BMMQ::InstructionRetirementDecision::continueSlice();
    }
};
void runTo(GB::GameBoyMachine& machine, std::uint16_t start, std::uint16_t end) {
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, start);
    Until until(machine, end);
    auto result = machine.runSlice({20000, 1000000, false}, &until);
    require(result.exitReason == BMMQ::ExecutionSliceExitReason::RetirementRequested, "guest routine failed to return");
}
}
int main(int argc, char** argv) try {
    require(argc == 3, "usage: time-smoke-pokered-species-patch ROM MOD_DIRECTORY");
    std::ifstream input(argv[1], std::ios::binary);
    require(bool(input), "cannot open ROM");
    const std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    const std::filesystem::path directories[]{argv[2]};
    auto loaded = BMMQ::Modding::loadModDirectories(directories, rom, "gameboy");
    if (!loaded.prepared) throw std::runtime_error(loaded.error);
    auto& prepared = *loaded.prepared;
    require(prepared.mods.size() == 1 && prepared.mods[0].id == "pokered.title-species", "wrong demo package");
    GB::GameBoyMachine machine;
    machine.loadRom(prepared.rom);
    machine.modHost() = std::move(prepared.host);
    auto& metadata = prepared.mods[0];
    for (const auto& [name, hook] : {std::pair{"NativeSpeciesSelect", 1u}, {"NativeSpeciesRead", 2u}}) {
        auto symbol = machine.modHost().resolveSymbol(name);
        require(symbol && symbol->bank == 0, "missing fixed-bank hook");
        auto module = BMMQ::Modding::NativeMod::load(metadata, machine.modHost());
        require(machine.installNativeTrampoline(symbol->address, std::move(module), hook), "cannot install hook");
    }
    auto& context = machine.runtimeContext();
    context.write8(0xFFFF, 0); // Isolate the real selector/header routines from boot/IRQs.
    context.write8(0xFF40, 0);
    context.write8(0x2000, 1);
    context.write8(0xFFB8, 1);
    const auto records = machine.modHost().region(metadata.regions.at("records"));
    const auto table = machine.modHost().region(metadata.regions.at("selection"));
    auto checkHeader = [&](std::uint8_t proxy, std::span<const std::uint8_t> expected) {
        context.write8(0xD0B5, proxy);
        context.writeRegister16(GB::RegisterId::AF, 0x5AB0);
        context.writeRegister16(GB::RegisterId::BC, 0x1234);
        context.writeRegister16(GB::RegisterId::DE, 0x5678);
        context.writeRegister16(GB::RegisterId::HL, 0xC46D);
        context.writeRegister16(GB::RegisterId::SP, 0xDFF0);
        runTo(machine, 0x452D, 0x4530); // Actual patched CALL in LoadTitleMonSprite.
        for (std::size_t i = 0; i < expected.size(); ++i)
            require(context.read8(0xD0B8 + i) == expected[i], "external header mismatch");
        require(context.readRegister16(GB::RegisterId::BC) == 0x1234 &&
                context.readRegister16(GB::RegisterId::DE) == 0x5678 &&
                context.readRegister16(GB::RegisterId::HL) == 0xC46D &&
                context.readRegister16(GB::RegisterId::SP) == 0xDFF0, "guest register/stack corruption");
    };
    // Before any selection, the initial title sprite must still use vanilla GetMonHeader.
    checkHeader(0x99, records.subspan(0x99 * 29 + 1, 28));
    checkHeader(0x15, records.subspan(0x15 * 29 + 1, 28)); // Mew's separate ROM header.
    for (unsigned slot = 0; slot < 16; ++slot) {
        context.writeRegister16(GB::RegisterId::BC, slot);
        context.writeRegister16(GB::RegisterId::SP, 0xDFF0);
        runTo(machine, 0x44A3, 0x44A8); // Real title selector, after its RNG chooses a slot.
        const auto species = table[slot * 2] | (unsigned(table[slot * 2 + 1]) << 8);
        const auto current = machine.modHost().region(metadata.regions.at("current"));
        require((current[0] | (unsigned(current[1]) << 8)) == species, "selection was truncated");
        const auto proxy = records[species * 29];
        require((context.readRegister16(GB::RegisterId::AF) >> 8) == proxy, "wrong guest display proxy");
        checkHeader(proxy, records.subspan(species * 29 + 1, 28));
        require(context.readRegister16(GB::RegisterId::AF) == 0x5AB0, "header shim changed AF");
        if (slot == 0) require(species == 256 && context.read8(0xD0B9) == 123, "extended species not used");
    }
    context.writeRegister16(GB::RegisterId::BC, 0);
    runTo(machine, 0x44A3, 0x44A8);
    records[256 * 29 + 2] = 124;
    checkHeader(0x99, records.subspan(256 * 29 + 1, 28));
    require(context.read8(0xD0B9) == 124, "guest did not observe a live external-pool edit");
    std::cout << "Real-ROM title patch: all 16 selections and headers passed; species 256 has external HP 123.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
