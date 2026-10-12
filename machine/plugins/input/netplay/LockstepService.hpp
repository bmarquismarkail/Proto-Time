#pragma once
#include "LockstepEngine.hpp"
#include "RemoteTransport.hpp"
#include "machine/plugins/debug/DebugMachine.hpp"
#include "space/CoreAdapter.hpp"
#include <memory>
#include <thread>

namespace BMMQ::Netplay {
enum class State : std::uint8_t { Waiting, Running, BudgetPaused, FrameComplete, Faulted };
struct Progress {
    State state{};
    Fault fault{};
    std::uint64_t frame{}, instructions{}, cycles{}, carry{};
    Digest before{}, after{};
};
// Sole machine-lane session owner. Peers acknowledge the complete pre-frame
// fingerprint before input publication or instruction execution. A missing
// packet pauses; it never reuses the preceding frame's input as a fallback.
class LockstepService {
    Machine& machine_;
    std::unique_ptr<Space::CoreAdapter> adapter_;
    Debug::IDebugMachineV1& control_;
    Debug::DebugEngine lease_{false};
    LockstepEngine engine_;
    RemoteTransport* transport_{};
    const std::thread::id owner_{std::this_thread::get_id()};
    std::uint64_t machineGeneration_{}, carry_{};
    std::uint8_t localPeer_{};
    bool started_{}, connected_{};
    std::optional<Packet> local_;
    Digest before_{};
    void lane() const;
    [[nodiscard]] bool compatible() const;
    void pump();
public:
    LockstepService(Machine&, Binding, std::uint8_t localPeer, RemoteTransport* = nullptr);
    ~LockstepService();
    LockstepService(const LockstepService&) = delete;
    LockstepService& operator=(const LockstepService&) = delete;
    [[nodiscard]] Packet localPacket(InputButtonMask); // Must precede frame execution.
    [[nodiscard]] bool submitLocal(InputButtonMask);
    [[nodiscard]] bool acceptRemote(const Packet&); // Owned transport data on machine lane.
    [[nodiscard]] Progress run(std::uint64_t maxInstructions = 4096);
    [[nodiscard]] bool acknowledged();
    void disconnect();
    [[nodiscard]] const LockstepEngine& engine() const { lane(); return engine_; }
    [[nodiscard]] Digest before() const { lane(); return before_; }
};
}
