#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <vector>

#include <unistd.h>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/GameBoyMemoryMap.hpp"
#include "machine/CPU.hpp"
#include "machine/SaveState.hpp"

namespace {

std::vector<uint8_t> makeRom()
{
    std::vector<uint8_t> rom(0x100000u, 0x00u);
    rom[0x0147u] = 0x13u; // MBC3 + RAM + battery
    rom[0x0148u] = 0x05u;
    rom[0x0149u] = 0x03u;
    rom[0x0100u] = 0x3Eu; // LD A,$42
    rom[0x0101u] = 0x42u;
    rom[0x0102u] = 0x00u;
    return rom;
}

std::vector<uint8_t> makeBankedMbc1Rom()
{
    constexpr std::size_t bankCount = 64u;
    constexpr std::size_t bankSize = 0x4000u;
    std::vector<uint8_t> rom(bankCount * bankSize, 0x00u);
    for (std::size_t bank = 0; bank < bankCount; ++bank) {
        std::fill_n(rom.begin() + static_cast<std::ptrdiff_t>(bank * bankSize),
                    bankSize,
                    static_cast<uint8_t>(bank));
    }
    rom[0x0147u] = 0x03u;
    rom[0x0148u] = 0x05u;
    rom[0x0149u] = 0x00u;
    return rom;
}

std::filesystem::path makeSavePath()
{
    std::ostringstream name;
    name << "proto-time-gb-machine-" << ::getpid() << "-" << ::getppid() << "-"
         << std::chrono::steady_clock::now().time_since_epoch().count() << ".ptss";
    return std::filesystem::temp_directory_path() / name.str();
}

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main()
{
    static_assert(static_cast<std::uint32_t>(BMMQ::ExecutionPathHint::PortableIr) == 3u);
    static_assert(static_cast<std::uint32_t>(BMMQ::ExecutionPathHint::NativeIr) == 4u);
    static_assert(static_cast<std::uint32_t>(BMMQ::ExecutionPathHint::CachedBlock) == 5u);
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
    const auto originalFingerprint = machine.deterministicStateFingerprint();
    assert(originalFingerprint.size() == 16u);
    const auto savedPc = machine.runtimeContext().readRegister16(GB::RegisterId::PC);

    machine.save_state(savePath);
    assert(std::filesystem::exists(savePath));

    machine.runtimeContext().writeRegister16(GB::RegisterId::BC, 0x1111u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0x2222u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x3333u);
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xFF24u, 0x00u);
    machine.setJoypadState(0x00u);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    machine.runtimeContext().write8(0xA000u, 0x99u);

    machine.load_state(savePath);

    assert(machine.deterministicStateFingerprint() == originalFingerprint);

    assert(machine.runtimeContext().readRegister16(GB::RegisterId::BC) == 0xBEEFu);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::SP) == 0xC123u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == savedPc);
    assert(machine.runtimeContext().read8(0xC000u) == 0xA5u);
    assert(machine.runtimeContext().read8(0xFF24u) == 0x77u);
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == 0x05u);
    machine.runtimeContext().write8(0x0000u, 0x0Au);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    assert(machine.runtimeContext().read8(0xA000u) == 0x12u);
    machine.runtimeContext().write8(0x4000u, 0x01u);
    assert(machine.runtimeContext().read8(0xA000u) == 0x34u);
    machine.runtimeContext().write8(0xC000u, 0xA4u);
    assert(machine.deterministicStateFingerprint() != originalFingerprint);

    GB::GameBoyMachine unloaded;
    bool rejectedWithoutRom = false;
    try {
        unloaded.load_state(savePath);
    } catch (const std::runtime_error&) {
        rejectedWithoutRom = true;
    }
    assert(rejectedWithoutRom);

    const auto mbc1Rom = makeBankedMbc1Rom();
    GB::GameBoyMachine mbc1Machine;
    mbc1Machine.loadRom(mbc1Rom);
    mbc1Machine.runtimeContext().write8(0x2000u, 0x01u);
    mbc1Machine.runtimeContext().write8(0x4000u, 0x01u);
    mbc1Machine.runtimeContext().write8(0x6000u, 0x01u);
    assert(mbc1Machine.runtimeContext().read8(0x0200u) == 0x20u);
    mbc1Machine.save_state(savePath);

    auto legacyState = BMMQ::SaveStateReader::read(savePath);
    GB::GameBoyMemoryMap legacyMemoryMap;
    legacyMemoryMap.installRomWindow(
        0x0000u, std::span<const uint8_t>(mbc1Rom.data(), 0x4000u));
    legacyMemoryMap.installRomWindow(
        0x4000u, std::span<const uint8_t>(mbc1Rom.data() + 0x4000u, 0x4000u));
    const auto staleMemory = legacyMemoryMap.exportState();
    bool replacedMemoryChunk = false;
    for (auto& chunk : legacyState.chunks) {
        if (chunk.name == "gb.memory") {
            chunk.data = staleMemory;
            chunk.size = static_cast<uint32_t>(chunk.data.size());
            replacedMemoryChunk = true;
            break;
        }
    }
    assert(replacedMemoryChunk);
    BMMQ::SaveStateReader::write(legacyState, savePath);
    mbc1Machine.load_state(savePath);
    assert(mbc1Machine.runtimeContext().read8(0x0200u) == 0x20u);

    removeIfExists(savePath);
    return 0;
}
