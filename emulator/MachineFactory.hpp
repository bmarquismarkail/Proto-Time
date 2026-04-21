#ifndef BMMQ_MACHINE_FACTORY_HPP
#define BMMQ_MACHINE_FACTORY_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "machine/Machine.hpp"

namespace BMMQ {

enum class MachineKind : std::uint8_t {
    GameBoy = 0,
    GameGear = 1,
};

struct MachineDescriptor {
    std::string_view id;
    std::string_view displayName;
    std::string_view captureTargetId;
    int defaultFrameWidth = 160;
    int defaultFrameHeight = 144;
    bool supportsExternalBootRom = false;
    bool supportsVisualPacks = false;
    bool supportsVisualCapture = false;
};

struct MachineInstance {
    MachineKind kind = MachineKind::GameBoy;
    MachineDescriptor descriptor{};
    std::unique_ptr<Machine> machine;
};

[[nodiscard]] MachineKind parseMachineKind(std::string_view value);
[[nodiscard]] const MachineDescriptor& machineDescriptor(MachineKind kind) noexcept;
[[nodiscard]] MachineInstance createMachine(MachineKind kind);

} // namespace BMMQ

#endif // BMMQ_MACHINE_FACTORY_HPP
