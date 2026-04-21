#include "emulator/MachineFactory.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"

namespace BMMQ {
namespace {

[[nodiscard]] std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

constexpr MachineDescriptor kGameBoyDescriptor{
    "gameboy",
    "Game Boy",
    "gameboy",
    160,
    144,
    true,
    true,
    true,
};

constexpr MachineDescriptor kGameGearDescriptor{
    "gamegear",
    "Game Gear",
    "gamegear",
    160,
    144,
    false,
    false,
    false,
};

[[noreturn]] void unreachableMachineDescriptor(MachineKind kind) noexcept
{
    (void)kind;
#ifndef NDEBUG
    assert(false && "Unhandled MachineKind in machineDescriptor");
#endif
    std::abort();
#if defined(__clang__) || defined(__GNUC__)
    __builtin_unreachable();
#endif
}

[[nodiscard]] std::string formatUnknownMachineKindMessage(std::string_view functionName, MachineKind kind)
{
    std::ostringstream oss;
    oss << functionName << " received unsupported MachineKind value "
        << static_cast<unsigned int>(kind);
    return oss.str();
}

} // namespace

MachineKind parseMachineKind(std::string_view value)
{
    const auto normalized = lowerAscii(std::string(value));
    if (normalized == "gameboy") {
        return MachineKind::GameBoy;
    }
    if (normalized == "gamegear") {
        return MachineKind::GameGear;
    }
    throw std::invalid_argument("Unknown machine core: " + std::string(value));
}

const MachineDescriptor& machineDescriptor(MachineKind kind) noexcept
{
    switch (kind) {
    case MachineKind::GameBoy:
        return kGameBoyDescriptor;
    case MachineKind::GameGear:
        return kGameGearDescriptor;
    }

    unreachableMachineDescriptor(kind);
}

MachineInstance createMachine(MachineKind kind)
{
    MachineInstance instance;
    instance.kind = kind;

    switch (kind) {
    case MachineKind::GameBoy:
        instance.descriptor = machineDescriptor(kind);
        instance.machine = std::make_unique<GameBoyMachine>();
        break;
    case MachineKind::GameGear:
        instance.descriptor = machineDescriptor(kind);
        instance.machine = std::make_unique<GameGearMachine>();
        break;
    default:
        throw std::runtime_error(formatUnknownMachineKindMessage("createMachine", kind));
    }

    return instance;
}

} // namespace BMMQ
