#pragma once

#include "emulator/RiverXmbIntegration.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace BMMQ {
class RuntimeContext;

// English retail Red only. A title string alone cannot establish the RAM layout.
[[nodiscard]] bool supportsPokemonRedTelemetry(std::span<const std::uint8_t> rom);

// Capture only on the machine owner lane, between execution slices. The snapshot
// owns all bytes; consumers cannot retain a view into mutable guest memory.
class PokemonRedSnapshot final {
public:
    [[nodiscard]] static PokemonRedSnapshot capture(const RuntimeContext& runtime);
    explicit PokemonRedSnapshot(std::array<std::uint8_t, 0x2000> wram) : wram_(wram) {}
    // A value exists only when this owned snapshot is a defensibly complete
    // gameplay state. Callers must not publish partial boot, title, or
    // transitional RAM to River XMB.
    [[nodiscard]] std::optional<RiverXmbTelemetry> telemetry() const;
private:
    std::array<std::uint8_t, 0x2000> wram_;
};
} // namespace BMMQ
