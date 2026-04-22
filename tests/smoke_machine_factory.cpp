#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
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
    assert(gameGearInstance.machine->visualDebugAdapter() == nullptr);
    assert(!gameGearInstance.machine->videoDebugFrameModel({
        .frameWidth = 160,
        .frameHeight = 144,
    }).has_value());

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
