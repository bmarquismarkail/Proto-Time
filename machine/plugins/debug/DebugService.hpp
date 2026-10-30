#pragma once
#include "DebugMachine.hpp"
#include "machine/plugins/script/ScriptHookEngine.hpp"
#include "machine/ExecutionSlice.hpp"
#include <thread>
namespace BMMQ {
class Machine;
}
namespace BMMQ::Debug {
enum class State : std::uint8_t { Detached, Paused, Running, Faulted };
enum class Reason : std::uint8_t {
  Entry,
  Pause,
  Step,
  Breakpoint,
  Watchpoint,
  Generation,
  Fault
};
enum class Operation : std::uint8_t {
  Inspect,
  Continue,
  Pause,
  Step,
  Rules,
  Edit,
  Disconnect
};
enum class Error : std::uint8_t {
  None,
  Detached,
  Running,
  Stale,
  Invalid,
  Unsupported,
  Fault
};
struct ByteEdit {
  std::uint16_t address{};
  std::uint8_t value{};
};
struct Command {
  std::uint32_t id{};
  Operation operation{};
  std::uint64_t generation{}, pauseId{};
  std::uint64_t steps{1};
  std::uint16_t address{}, length{};
  std::array<Breakpoint, 64> breaks{};
  std::array<Watchpoint, 64> watches{};
  std::uint16_t breakCount{}, watchCount{};
  bool editRegisters{};
  std::array<std::uint16_t, 20> registers{};
  std::array<ByteEdit, 64> bytes{};
  std::uint16_t byteCount{};
};
struct Reply {
  std::uint32_t id{};
  Error error{};
  State state{};
  Reason reason{};
  std::uint64_t generation{}, pauseId{}, lost{}, codeBacking{};
  std::uint64_t rejectedActions{}, requestOverflow{};
  std::array<std::uint16_t, 20> registers{};
  std::array<std::uint8_t, 256> memory{};
  std::uint16_t length{};
};
class DebugService {
  Machine &machine_;
  IDebugMachineV1 &adapter_;
  DebugEngine engine_;
  Script::ScriptHookEngine scriptHooks_;
  Queue<Command, 32> requests_;
  Queue<Reply, 32> replies_;
  std::optional<Reply> pendingReply_;
  const std::thread::id owner_ = std::this_thread::get_id();
  State state_{State::Paused};
  Reason reason_{Reason::Entry};
  std::uint64_t generation_{}, pauseId_{1}, remainingSteps_{};
  bool skipBreakpoint_{};
  bool connected_{true};
  std::uint64_t rejectedActions_{};
  std::atomic<std::uint64_t> requestOverflow_{0};
  void lane() const;
  void synchronize();
  void stop(Reason) noexcept;
  Reply snapshot(std::uint32_t id = 0) const;
  Reply apply(const Command &);

public:
  explicit DebugService(Machine &);
  DebugService(Machine &, bool recording);
  Reply currentSnapshot() { lane(); synchronize(); return snapshot(); }
  ~DebugService(); // Machine must outlive service; detach on the owner lane.
  DebugService(const DebugService &) = delete;
  DebugService &operator=(const DebugService &) = delete;
  // Exactly one transport producer/consumer. No machine reads on these APIs.
  bool request(const Command &command) noexcept {
    if (requests_.push(command))
      return true;
    requestOverflow_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  std::optional<Reply> response() noexcept { return replies_.pop(); }
  std::optional<TraceRecord> trace() noexcept { return engine_.consume(); }
  // Machine-lane APIs. Backpressure pauses instead of losing control replies.
  void pump();
  ExecutionSliceResult run(std::uint64_t maxInstructions = 64);
  bool installScriptHooks(std::span<const Script::HookAction> actions,Script::HookCapabilities caps) {
    lane();synchronize();return scriptHooks_.install(actions,caps,generation_,state_==State::Paused);
  }
  const Script::ScriptHookEngine& scriptHooks()const {lane();return scriptHooks_;}
  State state() const {
    lane();
    return state_;
  }
  std::span<const char *const> registerNames() const noexcept {
    return adapter_.debugRegisterNames();
  }
};
} // namespace BMMQ::Debug
