#ifndef BMMQ_MACHINE_FACTORY_HPP
#define BMMQ_MACHINE_FACTORY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "machine/Machine.hpp"

namespace BMMQ {

enum class MachineKind : std::uint8_t {
    GameBoy = 0,
    GameGear = 1,
};

struct MachineDescriptor {
    std::string id;
    std::string displayName;
    int defaultFrameWidth = 160;
    int defaultFrameHeight = 144;
};

class MachineRegistry {
public:
    using Factory = std::function<std::unique_ptr<Machine>()>;

    void registerProvider(MachineDescriptor descriptor, Factory factory);
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] const MachineDescriptor& descriptor(std::string_view id) const;
    [[nodiscard]] std::unique_ptr<Machine> create(std::string_view id) const;
    [[nodiscard]] std::vector<MachineDescriptor> descriptors() const;

    [[nodiscard]] static const MachineRegistry& builtins();

private:
    struct Provider {
        MachineDescriptor descriptor;
        Factory factory;
    };
    std::vector<Provider> providers_;
};

struct MachineInstance {
    MachineKind kind = MachineKind::GameBoy;
    MachineDescriptor descriptor{};
    std::unique_ptr<Machine> machine;
};

[[nodiscard]] MachineKind parseMachineKind(std::string_view value);
[[nodiscard]] const MachineDescriptor& machineDescriptor(MachineKind kind) noexcept;
[[nodiscard]] MachineInstance createMachine(MachineKind kind);
[[nodiscard]] MachineInstance createMachine(std::string_view id);

} // namespace BMMQ

#endif // BMMQ_MACHINE_FACTORY_HPP
