#include "ScriptService.hpp"
#include <fstream>
namespace BMMQ::Script {
void ScriptService::lane() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("ScriptService must stay on its tooling lane");
}
void ScriptService::failure(std::string error) {
  ++diagnostics_.invocationFailureCount;
  diagnostics_.lastRuntimeError = std::move(error);
}
ScriptService::~ScriptService() {
  if (pending_.valid())
    pending_.wait();
}
bool ScriptService::load(unsigned id, Language language, std::string source,
                         Permissions permissions, Limits limits,
                         ScriptCapabilitiesV1 caps) {
  lane();
  if (state_ == State::Active || pending_.valid() || awaitingCommit_ ||
      id >= units_.size()) {
    failure("script load lifecycle/slot rejected");
    return false;
  }
  if (!ScriptEngine::available(language)) {
    ++diagnostics_.disabledRuntimeCount;
    failure("script runtime unavailable");
    return false;
  }
  if (!caps.nonRealtimeOnly || !caps.snapshotAware || !caps.headlessSafe ||
      !caps.boundedExecution ||
      ((permissions.ram || permissions.registers) &&
       !caps.machineMutationAllowed)) {
    ++diagnostics_.permissionDeniedCount;
    failure("script capabilities rejected");
    return false;
  }
  if (!limits.sourceBytes || limits.sourceBytes > 1024 * 1024 ||
      source.size() > limits.sourceBytes ||
      source.find('\0') != std::string::npos || limits.heapBytes < 65536 ||
      limits.heapBytes > 64 * 1024 * 1024 || !limits.instructions ||
      limits.instructions > 100'000'000 || limits.timeout.count() < 1 ||
      limits.timeout.count() > 30000) {
    failure("script resource limits rejected");
    return false;
  }
  units_[id] = Unit{language, std::move(source), permissions, limits, caps};
  diagnostics_.activeRuntimeSummary =
      std::to_string(id) + ":" + std::to_string(unsigned(language));
  return true;
}
bool ScriptService::loadFile(unsigned id, Language language,
                             const std::filesystem::path &path,
                             Permissions permissions, Limits limits,
                             ScriptCapabilitiesV1 caps) {
  lane();
  try {
    auto size = std::filesystem::file_size(path);
    if (size > limits.sourceBytes || size > 1024 * 1024)
      throw std::invalid_argument("script file budget rejected");
    std::ifstream stream(path, std::ios::binary);
    std::string source(size, '\0');
    if (!stream.read(source.data(), source.size()))
      throw std::runtime_error("cannot read script file");
    return load(id, language, std::move(source), permissions, limits, caps);
  } catch (const std::exception &error) {
    failure(error.what());
    return false;
  }
}
bool ScriptService::unload(unsigned id) {
  lane();
  if (state_ == State::Active || pending_.valid() || awaitingCommit_ ||
      id >= units_.size()) {
    failure("script unload lifecycle/slot rejected");
    return false;
  }
  units_[id].reset();
  return true;
}
void ScriptService::observe(Snapshot snapshot) {
  lane();
  snapshot_ = std::move(snapshot);
  diagnostics_.lastObservedGeneration = snapshot_.state.generation;
  switch (snapshot_.state.state) {
  case Debug::State::Detached:
    state_ = State::Detached;
    break;
  case Debug::State::Paused:
    state_ = State::Paused;
    break;
  case Debug::State::Running:
    state_ = State::Active;
    break;
  case Debug::State::Faulted:
    state_ = State::Faulted;
    break;
  }
}
void ScriptService::detach() {
  lane();
  state_ = State::Detached;
  snapshot_ = {};
  awaitingCommit_ = 0;
}
bool ScriptService::invoke(unsigned id) {
  lane();
  if (state_ != State::Paused || pending_.valid() || awaitingCommit_ ||
      id >= units_.size() || !units_[id]) {
    failure("script invocation lifecycle/slot rejected");
    return false;
  }
  pendingSlot_ = id;
  generation_ = snapshot_.state.generation;
  pause_ = snapshot_.state.pauseId;
  ++ticket_;
  auto unit = *units_[id];
  auto snapshot = snapshot_;
  try {
    pending_ = std::async(
        std::launch::async,
        [unit = std::move(unit), snapshot = std::move(snapshot)]() mutable {
          return ScriptEngine::evaluate(unit.language, unit.source,
                                        std::move(snapshot), unit.permissions,
                                        unit.limits);
        });
    return true;
  } catch (const std::exception &error) {
    failure(error.what());
    return false;
  }
}
bool ScriptService::invokeLive(unsigned) {
  lane();
  ++diagnostics_.rejectedLiveCallbackCount;
  failure("interpreters are unavailable on the live machine lane; use prepared "
          "hook actions");
  return false;
}
std::optional<Action> ScriptService::poll() {
  lane();
  if (!pending_.valid() || pending_.wait_for(std::chrono::milliseconds(0)) !=
                               std::future_status::ready)
    return {};
  Result result;
  try {
    result = pending_.get();
  } catch (const std::exception &error) {
    result.error = error.what();
  }
  if (state_ != State::Paused || snapshot_.state.generation != generation_ ||
      snapshot_.state.pauseId != pause_) {
    result = {.error = "stale script generation/pause; recipe discarded"};
  }
  if (!result.success) {
    if (result.permissionDenied)
      ++diagnostics_.permissionDeniedCount;
    if (result.runtimeUnavailable) {
      ++diagnostics_.disabledRuntimeCount;
      units_[pendingSlot_].reset();
    }
    failure(result.error);
  } else if (result.staged.editRegisters || result.staged.byteCount)
    awaitingCommit_ = ticket_;
  return Action{ticket_, std::move(result)};
}
bool ScriptService::acknowledge(std::uint64_t ticket,
                                const Debug::Reply &reply) {
  lane();
  if (!awaitingCommit_ || awaitingCommit_ != ticket) {
    failure("script commit ticket rejected");
    return false;
  }
  awaitingCommit_ = 0;
  if (reply.error != Debug::Error::None) {
    failure("machine rejected script recipe: " +
            std::to_string(unsigned(reply.error)));
    return false;
  }
  auto snapshot = snapshot_;
  snapshot.state = reply;
  snapshot.state.length = 0;
  observe(std::move(snapshot));
  return true;
}
} // namespace BMMQ::Script
