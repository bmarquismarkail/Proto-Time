#pragma once
#include "ScriptPlugin.hpp"
#include <filesystem>
#include <future>
#include <optional>
#include <thread>
namespace BMMQ::Script {
enum class State { Detached, Paused, Active, Faulted };
struct Diagnostics {
  std::uint64_t invocationFailureCount{}, rejectedLiveCallbackCount{},
      permissionDeniedCount{}, disabledRuntimeCount{}, lastObservedGeneration{};
  std::string lastRuntimeError, activeRuntimeSummary;
};
struct Action {
  std::uint64_t ticket{};
  Result result;
};
// Owned by one tooling producer. Poll returns a recipe, which that producer
// multiplexes into DebugService's existing single-producer command queue.
class ScriptService {
  struct Unit {
    Language language;
    std::string source;
    Permissions permissions;
    Limits limits;
    ScriptCapabilitiesV1 capabilities;
  };
  const std::thread::id owner_ = std::this_thread::get_id();
  std::array<std::optional<Unit>, 8> units_{};
  Snapshot snapshot_;
  State state_{State::Detached};
  Diagnostics diagnostics_;
  std::future<Result> pending_;
  std::uint64_t generation_{}, pause_{}, ticket_{}, awaitingCommit_{};
  unsigned pendingSlot_{};
  void lane() const;
  void failure(std::string);

public:
  ~ScriptService(); // Join bounded interpreter work on the tooling lane.
  bool load(unsigned, Language, std::string, Permissions = {}, Limits = {},
            ScriptCapabilitiesV1 = {});
  bool loadFile(unsigned, Language, const std::filesystem::path &,
                Permissions = {}, Limits = {}, ScriptCapabilitiesV1 = {});
  bool unload(unsigned);
  void observe(Snapshot);
  void detach();
  bool invoke(unsigned);
  bool invokeLive(unsigned); // Always rejected; use prevalidated HookAction.
  std::optional<Action> poll();
  bool acknowledge(std::uint64_t, const Debug::Reply &);
  const Diagnostics &diagnostics() const {
    lane();
    return diagnostics_;
  }
  State state() const {
    lane();
    return state_;
  }
};
} // namespace BMMQ::Script
