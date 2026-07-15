#include "emulator/MachineFactory.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;
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

const MachineDescriptor kGameBoyDescriptor{
    "gameboy",
    "Game Boy",
    160,
    144,
};

const MachineDescriptor kGameGearDescriptor{
    "gamegear",
    "Game Gear",
    160,
    144,
};

[[noreturn]] void unreachableMachineDescriptor(MachineKind kind) noexcept
{
    (void)kind;
    assert(false && "Unhandled MachineKind in machineDescriptor");
#if defined(NDEBUG)
#  if defined(__clang__) || defined(__GNUC__)
    __builtin_unreachable();
#  else
    std::abort();
#  endif
#endif
}

} // namespace

void MachineRegistry::registerProvider(MachineDescriptor descriptor, Factory factory)
{
    if (descriptor.id.empty()) {
        throw std::invalid_argument("machine provider id is empty");
    }
    if (descriptor.displayName.empty()) {
        throw std::invalid_argument("machine provider display name is empty");
    }
    if (!factory) {
        throw std::invalid_argument("machine provider factory is empty");
    }
    if (contains(descriptor.id)) {
        throw std::invalid_argument("duplicate machine provider id: " + descriptor.id);
    }
    providers_.push_back(Provider{std::move(descriptor), std::move(factory)});
}

bool MachineRegistry::contains(std::string_view id) const noexcept
{
    return std::any_of(providers_.begin(), providers_.end(), [id](const auto& provider) {
        return provider.descriptor.id == id;
    });
}

const MachineDescriptor& MachineRegistry::descriptor(std::string_view id) const
{
    const auto found = std::find_if(providers_.begin(), providers_.end(), [id](const auto& provider) {
        return provider.descriptor.id == id;
    });
    if (found == providers_.end()) {
        throw std::invalid_argument("Unknown machine core: " + std::string(id));
    }
    return found->descriptor;
}

std::unique_ptr<Machine> MachineRegistry::create(std::string_view id) const
{
    const auto found = std::find_if(providers_.begin(), providers_.end(), [id](const auto& provider) {
        return provider.descriptor.id == id;
    });
    if (found == providers_.end()) {
        throw std::invalid_argument("Unknown machine core: " + std::string(id));
    }
    auto machine = found->factory();
    if (!machine) {
        throw std::runtime_error("machine provider returned null: " + found->descriptor.id);
    }
    return machine;
}

std::vector<MachineDescriptor> MachineRegistry::descriptors() const
{
    std::vector<MachineDescriptor> result;
    result.reserve(providers_.size());
    for (const auto& provider : providers_) result.push_back(provider.descriptor);
    return result;
}

const MachineRegistry& MachineRegistry::builtins()
{
    static const MachineRegistry registry = [] {
        MachineRegistry result;
        result.registerProvider(kGameBoyDescriptor, [] { return std::make_unique<GameBoyMachine>(); });
        result.registerProvider(kGameGearDescriptor, [] { return std::make_unique<GameGearMachine>(); });
        return result;
    }();
    return registry;
}

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
    if (kind != MachineKind::GameBoy && kind != MachineKind::GameGear) {
        throw std::runtime_error("createMachine received unsupported MachineKind value " +
                                 std::to_string(static_cast<unsigned int>(kind)));
    }
    MachineInstance instance;
    instance.kind = kind;
    instance.descriptor = machineDescriptor(kind);
    instance.machine = MachineRegistry::builtins().create(instance.descriptor.id);
    return instance;
}

MachineInstance createMachine(std::string_view id)
{
    const auto kind = parseMachineKind(id);
    return createMachine(kind);
}

} // namespace BMMQ
