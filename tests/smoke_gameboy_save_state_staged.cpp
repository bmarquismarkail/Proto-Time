#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"

#include "machine/SaveState.hpp"

namespace {

std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(0x100000u, 0x00u);
    rom[0x0147u] = 0x13u; // MBC3 + RAM + battery
    rom[0x0148u] = 0x05u;
    rom[0x0149u] = 0x03u;
    rom[0x0100u] = 0x3Eu; // LD A,$42
    rom[0x0101u] = 0x42u;
    rom[0x0102u] = 0x00u;
    return rom;
}

std::filesystem::path makeSavePath() {
    static std::atomic<uint64_t> sequence{0u};
    std::ostringstream name;
    name << "proto-time-gb-staged-"
         << std::chrono::steady_clock::now().time_since_epoch().count() << "-"
         << sequence.fetch_add(1u, std::memory_order_relaxed) << ".ptss";
    return std::filesystem::temp_directory_path() / name.str();
}

void removeIfExists(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void truncateChunkPayload(std::filesystem::path path, std::string_view name) {
    auto state = BMMQ::SaveStateReader::read(path);
    for (auto& chunk : state.chunks) {
        if (chunk.name == name && !chunk.data.empty()) {
            chunk.data.pop_back();
            break;
        }
    }
    BMMQ::SaveStateReader::write(state, path);
}

} // namespace

int main() {
    const auto savePath = makeSavePath();
    removeIfExists(savePath);

    const auto rom = makeRom();
    GB::GameBoyMachine machine;
    machine.loadRom(rom);
    machine.runtimeContext().writeRegister16(GB::RegisterId::BC, 0xBEEFu);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0xC123u);
    machine.runtimeContext().write8(0xC000u, 0xA5u);
    machine.runtimeContext().write8(0xFF26u, 0x80u);
    machine.runtimeContext().write8(0xFF24u, 0x77u);
    machine.setJoypadState(0x05u);
    machine.runtimeContext().write8(0x0000u, 0x0Au);
    machine.runtimeContext().write8(0xA000u, 0x12u);
    machine.runtimeContext().write8(0x4000u, 0x01u);
    machine.runtimeContext().write8(0xA000u, 0x34u);
    machine.step();

    machine.save_state(savePath);
    assert(std::filesystem::exists(savePath));

    // Corruption that should make APU deserialization fail.
    truncateChunkPayload(savePath, "gb.apu");

    // Mutate machine after save so a staged restore failure would leave it in this
    // changed state instead of the pre-load state if the import were not atomic.
    machine.runtimeContext().writeRegister16(GB::RegisterId::BC, 0x1111u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0x2222u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x3333u);
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xFF24u, 0x00u);
    machine.setJoypadState(0x00u);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    machine.runtimeContext().write8(0xA000u, 0x99u);

    bool loadThrew = false;
    try {
        machine.load_state(savePath);
    } catch (const std::exception&) {
        loadThrew = true;
    }
    assert(loadThrew);

    // After failed load_state, the machine must still be in the mutated state,
    // confirming that no partial restore committed any chunk from the save.
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::BC) == 0x1111u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::SP) == 0x2222u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x3333u);
    assert(machine.runtimeContext().read8(0xC000u) == 0x00u);
    assert(machine.runtimeContext().read8(0xFF24u) == 0x00u);
    assert(!machine.currentDigitalInputMask().has_value() ||
           *machine.currentDigitalInputMask() == 0x00u);
    machine.runtimeContext().write8(0x0000u, 0x0Au);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    assert(machine.runtimeContext().read8(0xA000u) == 0x99u);

    removeIfExists(savePath);
    return 0;
}
