#ifndef BMMQ_EMULATOR_HOST_HPP
#define BMMQ_EMULATOR_HOST_HPP

#include <cstddef>
#include <memory>

#include "emulator/EmulatorConfig.hpp"
#include "emulator/MachineFactory.hpp"

namespace BMMQ {

struct BootstrappedMachine {
    MachineKind kind = MachineKind::GameBoy;
    MachineDescriptor descriptor{};
    std::unique_ptr<Machine> machine;
    std::size_t romSize = 0u;
    bool captureStarted = false;

    BootstrappedMachine() = default;
    BootstrappedMachine(BootstrappedMachine&&) noexcept = default;
    BootstrappedMachine& operator=(BootstrappedMachine&&) noexcept = default;
    BootstrappedMachine(const BootstrappedMachine&) = delete;
    BootstrappedMachine& operator=(const BootstrappedMachine&) = delete;

    ~BootstrappedMachine();
};

[[nodiscard]] BootstrappedMachine bootstrapMachine(const EmulatorConfig& options);

} // namespace BMMQ

#endif // BMMQ_EMULATOR_HOST_HPP
