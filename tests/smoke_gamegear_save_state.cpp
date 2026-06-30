#include <cassert>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/SaveState.hpp"

namespace {

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void appendU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(out.good());
}

} // namespace

int main()
{
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    const auto uniqueName = std::filesystem::path("proto-time-gg-machine-") += std::to_string(dist(rd)) += ".ptss";
    const auto savePath = std::filesystem::temp_directory_path() / uniqueName;
    removeIfExists(savePath);

    std::vector<uint8_t> rom(0x8000u, 0x00u);
    rom[0x0000u] = 0x3Eu; // LD A,$42
    rom[0x0001u] = 0x42u;

    BMMQ::GameGearMachine machine;
    machine.loadRom(rom);
    machine.runtimeContext().writeRegister16("BC", 0xBEEFu);
    machine.runtimeContext().writeRegister16("SP", 0xC123u);
    machine.runtimeContext().write8(0xC000u, 0xA5u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    machine.runtimeContext().write8(0x8000u, 0x56u);
    machine.runtimeContext().write8(0xBFFFu, 0x78u);
    machine.runtimeContext().write8(0xFF10u, 0x80u);
    machine.runtimeContext().write8(0x8000u, 0x9Au); // VDP VRAM path through memory map
    machine.serviceInput();
    machine.step();
    const auto savedPc = machine.runtimeContext().readRegister16("PC");

    machine.save_state(savePath);
    assert(std::filesystem::exists(savePath));

    machine.runtimeContext().writeRegister16("BC", 0x1111u);
    machine.runtimeContext().writeRegister16("SP", 0x2222u);
    machine.runtimeContext().writeRegister16("PC", 0x3333u);
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    machine.runtimeContext().write8(0x8000u, 0x00u);

    machine.load_state(savePath);

    assert(machine.runtimeContext().readRegister16("BC") == 0xBEEFu);
    assert(machine.runtimeContext().readRegister16("SP") == 0xC123u);
    assert(machine.runtimeContext().readRegister16("PC") == savedPc);
    assert(machine.runtimeContext().read8(0xC000u) == 0xA5u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    assert(machine.runtimeContext().read8(0xBFFFu) == 0x78u);

    auto corruptState = BMMQ::SaveStateReader::read(savePath);
    for (auto& chunk : corruptState.chunks) {
        if (chunk.name == "gg.machine") {
            assert(chunk.data.size() >= 18u);
            chunk.data[16u] = 2u; // invalid interruptRequested boolean after importable chunks.
            break;
        }
    }
    const auto corruptPath = savePath;
    BMMQ::SaveStateReader::write(corruptState, corruptPath);
    machine.runtimeContext().writeRegister16("BC", 0x1111u);
    bool rejectedCorruptMetadata = false;
    try {
        machine.load_state(corruptPath);
    } catch (const std::invalid_argument&) {
        rejectedCorruptMetadata = true;
    }
    assert(rejectedCorruptMetadata);
    assert(machine.runtimeContext().readRegister16("BC") == 0x1111u);

    BMMQ::GameGearMachine unloaded;
    bool rejectedWithoutRom = false;
    try {
        unloaded.load_state(savePath);
    } catch (const std::runtime_error&) {
        rejectedWithoutRom = true;
    }
    assert(rejectedWithoutRom);

    std::vector<uint8_t> hugeChunkCount;
    hugeChunkCount.insert(hugeChunkCount.end(), BMMQ::kSaveStateMagic, BMMQ::kSaveStateMagic + 5u);
    appendU32(hugeChunkCount, BMMQ::kSaveStateVersion);
    appendU32(hugeChunkCount, BMMQ::kCoreId_GameGear);
    appendU32(hugeChunkCount, 0u);
    appendU32(hugeChunkCount, static_cast<uint32_t>(BMMQ::SaveStateChecksum::None));
    appendU32(hugeChunkCount, 1000000u);
    const auto hugePath = savePath;
    writeBytes(hugePath, hugeChunkCount);
    bool rejectedHugeChunkCount = false;
    try {
        (void)BMMQ::SaveStateReader::read(hugePath);
    } catch (const std::runtime_error& ex) {
        rejectedHugeChunkCount = std::string(ex.what()).find("chunk count") != std::string::npos;
    }
    assert(rejectedHugeChunkCount);

    removeIfExists(savePath);
    return 0;
}
