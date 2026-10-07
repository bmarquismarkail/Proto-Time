#pragma once
#include "../InputSnapshotPluginV1.hpp"
#include <array>
#include <memory>
#include <string>

namespace BMMQ {
struct LinuxControllerAxis {
    std::uint16_t code{};
    std::int32_t minimum{-32768}, maximum{32767}, deadzone{4096};
};
struct LinuxControllerProfile {
    // Linux evdev codes, in InputButton bit order. No host labels enter a core.
    std::array<std::uint16_t, 8> buttons{0x223, 0x222, 0x220, 0x221, 0x130, 0x131, 0x13a, 0x13b};
    std::array<LinuxControllerAxis, 4> axes{{{0}, {1}, {3}, {4}}};
    std::uint16_t horizontalHat{0x10}, verticalHat{0x11};
};
enum class LinuxControllerError : std::uint8_t {
    None, DeviceUnavailable, Disconnected, Overflow, MalformedStream,
    ResyncUnavailable, InvalidProfile, WorkerFailure
};
struct LinuxControllerDiagnostics {
    std::uint64_t reports{}, events{}, overflows{}, connections{}, droppedSynchronizations{}, staleSnapshots{};
    LinuxControllerError error{};
    bool connected{};
};
class LinuxController final : public IInputSnapshotSourceV1 {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit LinuxController(std::string devicePath, LinuxControllerProfile profile = {});
    // Tool/test event stream, not physical-controller acceptance. Duplicates a
    // caller-owned nonblocking descriptor; never changes its status flags.
    explicit LinuxController(int eventStreamFd, LinuxControllerProfile profile = {});
    ~LinuxController() override;
    LinuxController(const LinuxController&) = delete;
    LinuxController& operator=(const LinuxController&) = delete;
    [[nodiscard]] InputPluginCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool open() override;
    void close() noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] bool boundedBoundarySamplingV1() const noexcept override { return true; }
    [[nodiscard]] InputBoundarySnapshotV1 sampleBoundaryV1(std::uint64_t generation) noexcept override;
    [[nodiscard]] LinuxControllerDiagnostics diagnostics() const noexcept;
};
}
