#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "emulator/MachineFactory.hpp"

int main()
{
    using BMMQ::MachineKind;

    assert(BMMQ::parseMachineKind("gameboy") == MachineKind::GameBoy);
    assert(BMMQ::parseMachineKind("gamegear") == MachineKind::GameGear);

    const auto& gameBoyDescriptor = BMMQ::machineDescriptor(MachineKind::GameBoy);
    assert(gameBoyDescriptor.id == "gameboy");
    const auto& gameGearDescriptor = BMMQ::machineDescriptor(MachineKind::GameGear);
    assert(gameGearDescriptor.id == "gamegear");

    auto gameBoyInstance = BMMQ::createMachine(MachineKind::GameBoy);
    assert(gameBoyInstance.machine != nullptr);
    assert(gameBoyInstance.descriptor.id == "gameboy");
    (void)gameBoyInstance.machine->pluginManager();
    assert(gameBoyInstance.machine->clockHz() != 0u);
    assert(gameBoyInstance.machine->supportsVisualPacks());
    assert(gameBoyInstance.machine->supportsVisualCapture());
    assert(gameBoyInstance.machine->visualTargetId() == "gameboy");
    assert(gameBoyInstance.machine->visualDebugAdapter() != nullptr);
    std::vector<std::uint8_t> gameBoyRom(0x8000u, 0x00u);
    gameBoyRom[0x0100u] = 0x00u;
    gameBoyInstance.machine->loadRom(gameBoyRom);
    auto gameBoyModel = gameBoyInstance.machine->videoDebugFrameModel({
        .frameWidth = 8,
        .frameHeight = 8,
    });
    assert(gameBoyModel.has_value());
    assert(gameBoyModel->width == 8);
    assert(gameBoyModel->height == 8);

    auto gameGearInstance = BMMQ::createMachine(MachineKind::GameGear);
    assert(gameGearInstance.machine != nullptr);
    assert(gameGearInstance.descriptor.id == "gamegear");
    (void)gameGearInstance.machine->pluginManager();
    assert(gameGearInstance.machine->clockHz() != 0u);
    assert(!gameGearInstance.machine->supportsVisualPacks());
    assert(!gameGearInstance.machine->supportsVisualCapture());
    assert(gameGearInstance.machine->visualTargetId().empty());
    std::vector<std::uint8_t> gameGearRom(0x4000u, 0x00u);
    std::size_t pc = 0u;
    auto emit8 = [&](std::uint8_t value) {
        gameGearRom[pc++] = value;
    };
    auto emit16 = [&](std::uint16_t value) {
        emit8(static_cast<std::uint8_t>(value & 0x00FFu));
        emit8(static_cast<std::uint8_t>((value >> 8) & 0x00FFu));
    };
    auto emitLdHlImm = [&](std::uint16_t address) {
        emit8(0x21u);
        emit16(address);
    };
    auto emitLdMemHlImm8 = [&](std::uint8_t value) {
        emit8(0x36u);
        emit8(value);
    };

    emitLdHlImm(0xFF40u);
    emitLdMemHlImm8(0x04u); // VDP mode 4
    emitLdHlImm(0xFF41u);
    emitLdMemHlImm8(0x40u); // display visible
    emitLdHlImm(0xFF42u);
    emitLdMemHlImm8(0x06u); // name table at 0x1800 for this proof ROM
    emitLdHlImm(0xFF47u);
    emitLdMemHlImm8(0xE4u);
    emitLdHlImm(0x8000u);
    for (int row = 0; row < 8; ++row) {
        emitLdMemHlImm8(0xFFu);
        emit8(0x23u);
        emitLdMemHlImm8(0x00u);
        emit8(0x23u);
    }
    emitLdHlImm(0x9810u);
    emitLdMemHlImm8(0x00u);
    emitLdHlImm(0x9801u);
    emitLdMemHlImm8(0x01u);
    const std::uint16_t jpTarget = static_cast<std::uint16_t>(pc);
    emit8(0xC3u);
    emit16(jpTarget);

    gameGearInstance.machine->loadRom(gameGearRom);
    for (int i = 0; i < 512; ++i) {
        gameGearInstance.machine->step();
    }
    auto gameGearModel = gameGearInstance.machine->videoDebugFrameModel({
        .frameWidth = 160,
        .frameHeight = 144,
    });
    assert(gameGearModel.has_value());
    assert(gameGearModel->width == 160);
    assert(gameGearModel->height == 144);
    assert(gameGearModel->displayEnabled);
    std::unordered_set<std::uint32_t> uniquePixels(
        gameGearModel->argbPixels.begin(),
        gameGearModel->argbPixels.end());
    assert(uniquePixels.size() > 1u);

    bool invalidKindRejected = false;
    try {
        (void)BMMQ::createMachine(static_cast<MachineKind>(0xFFu));
    } catch (const std::runtime_error&) {
        invalidKindRejected = true;
    }
    assert(invalidKindRejected);

    bool badCoreRejected = false;
    try {
        (void)BMMQ::parseMachineKind("nope");
    } catch (const std::invalid_argument&) {
        badCoreRejected = true;
    }
    assert(badCoreRejected);

    return 0;
}
