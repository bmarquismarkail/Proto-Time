#pragma once
#include "machine/InputTypes.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace BMMQ::Netplay {
using Digest = std::array<std::uint8_t, 32>;
enum class Core : std::uint8_t { GameBoy = 1, GameGear = 2 };
enum class Fault : std::uint8_t {
    None, InvalidPacket, BindingMismatch, ConflictingInput, FingerprintMismatch,
    WindowExhausted, HandoffOverflow, Disconnected, GenerationChanged,
    IncompatibleMachine, ExecutionFailure, FrameOverrun
};
struct Binding {
    Core core{};
    Digest session{}, rom{}, configuration{};
    std::uint64_t generation{};
    std::array<InputButtonMask, 2> ownership{0xff, 0};
    [[nodiscard]] bool operator==(const Binding&) const = default;
};
struct Packet {
    Binding binding{};
    std::uint64_t frame{};
    Digest before{};
    InputButtonMask input{};
    std::uint8_t peer{};
    [[nodiscard]] bool operator==(const Packet&) const = default;
};
// Fixed network format, big-endian integers, zero reserved bytes and a SHA-256
// integrity trailer. This is a trusted-peer protocol, not authentication.
using WirePacket = std::array<std::uint8_t, 196>;
[[nodiscard]] WirePacket encode(const Packet&);
[[nodiscard]] std::optional<Packet> decode(std::span<const std::uint8_t>);
[[nodiscard]] Digest digestFromHex(std::string_view);
[[nodiscard]] Digest stateDigest(std::string_view coreFingerprint);
[[nodiscard]] bool valid(const Binding&) noexcept;
[[nodiscard]] std::uint64_t cyclesPerFrame(Core) noexcept;
class LockstepEngine {
    struct Frame {
        std::uint64_t number{};
        bool occupied{};
        std::uint8_t received{};
        std::array<Packet, 2> packets{};
    };
    Binding binding_;
    std::array<Frame, 128> frames_{};
    std::array<Frame, 128> history_{};
    std::uint64_t next_{}, duplicates_{}, stale_{};
    Fault fault_{};
    bool ready_{};
public:
    explicit LockstepEngine(Binding);
    [[nodiscard]] bool accept(const Packet&) noexcept;
    [[nodiscard]] std::optional<InputButtonMask> ready(const Digest& before) noexcept;
    [[nodiscard]] bool completeFrame() noexcept;
    void fail(Fault fault) noexcept { if (fault_ == Fault::None) fault_ = fault; }
    [[nodiscard]] Fault fault() const noexcept { return fault_; }
    [[nodiscard]] std::uint64_t frame() const noexcept { return next_; }
    [[nodiscard]] std::uint64_t duplicates() const noexcept { return duplicates_; }
    [[nodiscard]] std::uint64_t stale() const noexcept { return stale_; }
    [[nodiscard]] const Binding& binding() const noexcept { return binding_; }
};
}
