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
    assert(gameBoyDescriptor.supportsExternalBootRom);
    assert(gameBoyDescriptor.supportsVisualPacks);
    assert(gameBoyDescriptor.supportsVisualCapture);

    const auto& gameGearDescriptor = BMMQ::machineDescriptor(MachineKind::GameGear);
    assert(gameGearDescriptor.id == "gamegear");
    assert(!gameGearDescriptor.supportsExternalBootRom);
    assert(!gameGearDescriptor.supportsVisualPacks);
    assert(!gameGearDescriptor.supportsVisualCapture);

    auto gameBoyInstance = BMMQ::createMachine(MachineKind::GameBoy);
    assert(gameBoyInstance.machine != nullptr);
    assert(gameBoyInstance.descriptor.id == "gameboy");
    (void)gameBoyInstance.machine->pluginManager();
    assert(gameBoyInstance.machine->clockHz() != 0u);

    auto gameGearInstance = BMMQ::createMachine(MachineKind::GameGear);
    assert(gameGearInstance.machine != nullptr);
    assert(gameGearInstance.descriptor.id == "gamegear");
    (void)gameGearInstance.machine->pluginManager();
    assert(gameGearInstance.machine->clockHz() != 0u);

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
