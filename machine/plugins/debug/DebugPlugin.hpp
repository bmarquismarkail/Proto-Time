#pragma once
#include "DebugService.hpp"
#include <memory>
#include <stdexcept>
#include <string>
namespace BMMQ::Debug {
// Separately versioned tooling contracts; no change to external machine ABIs.
struct DebugCapabilitiesV1 {
  bool snapshotAware{true};
  bool canRequestPause{};
  bool supportsExecutionControl{};
  bool supportsBreakpoints{};
  bool supportsWatchpoints{};
  bool deterministic{true};
  bool nonRealtimeOnly{true};
  bool hostThreadAffinity{true};
};
class IDebugBackendV1 {
public:
  virtual ~IDebugBackendV1() = default;
  virtual std::string_view name() const noexcept = 0;
  virtual DebugCapabilitiesV1 capabilities() const noexcept = 0;
  virtual bool open() = 0;
  virtual void close() noexcept = 0;
};
class IDebuggerPluginV1 : public virtual IDebugBackendV1 {
public:
  virtual std::optional<Command> inspect(const Reply &) = 0;
};
class ITraceSinkPluginV1 : public virtual IDebugBackendV1 {
public:
  virtual bool consume(const TraceRecord &) = 0;
};
class IWatchpointPluginV1 : public virtual IDebugBackendV1 {
public:
  virtual std::optional<Command> rules(const Reply &) = 0;
};
struct BackendDiagnostics {
  std::uint64_t rejectedActions{}, compatibilityBypasses{}, sinkFailures{},
      lostActions{};
  std::string lastBackendError;
};
// Tooling-lane service: prepared copies only, with bounded backend/action
// counts. No backend callback is invoked from DebugEngine or the guest machine
// lane.
class DebugBackendService {
  struct Entry {
    std::unique_ptr<IDebugBackendV1> backend;
    bool enabled{};
  };
  std::array<Entry, 8> entries_;
  Queue<Command, 32> actions_;
  const std::thread::id owner_ = std::this_thread::get_id();
  BackendDiagnostics diagnostics_;
  void lane() const {
    if (std::this_thread::get_id() != owner_)
      throw std::logic_error("debug backend lane changed");
  }
  void failed(Entry &entry) noexcept {
    entry.enabled = false;
    entry.backend->close();
    ++diagnostics_.sinkFailures;
  }
  void admitAction(const Entry &entry, const Command &command,
                   const Reply &snapshot) {
    auto caps = entry.backend->capabilities();
    bool allowed = command.generation == snapshot.generation &&
                   command.pauseId == snapshot.pauseId;
    switch (command.operation) {
    case Operation::Pause:
      allowed = allowed && caps.canRequestPause;
      break;
    case Operation::Step:
    case Operation::Continue:
      allowed = allowed && caps.supportsExecutionControl;
      break;
    case Operation::Rules:
      allowed = allowed && (!command.breakCount || caps.supportsBreakpoints) &&
                (!command.watchCount || caps.supportsWatchpoints);
      break;
    case Operation::Inspect:
      break;
    default:
      allowed = false;
      break; // Only the explicit control service may admit edits/disconnect.
    }
    if (!allowed) {
      ++diagnostics_.rejectedActions;
      return;
    }
    if (!actions_.push(command))
      ++diagnostics_.lostActions;
  }

public:
  void clear() {
    lane();
    for (auto &entry : entries_) {
      if (entry.enabled)
        entry.backend->close();
      entry = {};
    }
    while (actions_.pop()) {
    }
  }
  ~DebugBackendService() {
    for (auto &entry : entries_)
      if (entry.enabled)
        entry.backend->close();
  }
  bool attach(std::unique_ptr<IDebugBackendV1> backend) {
    lane();
    if (!backend)
      return false;
    auto caps = backend->capabilities();
    if (!caps.snapshotAware || !caps.deterministic || !caps.nonRealtimeOnly) {
      ++diagnostics_.compatibilityBypasses;
      diagnostics_.lastBackendError = "incompatible debug backend capabilities";
      return false;
    }
    auto found = std::find_if(entries_.begin(), entries_.end(),
                              [](const auto &entry) { return !entry.backend; });
    if (found == entries_.end()) {
      ++diagnostics_.compatibilityBypasses;
      diagnostics_.lastBackendError = "debug backend budget exceeded";
      return false;
    }
    try {
      if (!backend->open()) {
        backend->close();
        diagnostics_.lastBackendError = "debug backend open rejected";
        return false;
      }
    } catch (const std::exception &error) {
      backend->close();
      diagnostics_.lastBackendError = error.what();
      ++diagnostics_.sinkFailures;
      return false;
    } catch (...) {
      backend->close();
      diagnostics_.lastBackendError = "debug backend open failed";
      ++diagnostics_.sinkFailures;
      return false;
    }
    *found = {std::move(backend), true};
    return true;
  }
  void publish(const Reply &snapshot) {
    lane();
    for (auto &entry : entries_)
      if (entry.enabled) {
        try {
          if (auto *debugger =
                  dynamic_cast<IDebuggerPluginV1 *>(entry.backend.get()))
            if (auto action = debugger->inspect(snapshot))
              admitAction(entry, *action, snapshot);
          if (auto *watches =
                  dynamic_cast<IWatchpointPluginV1 *>(entry.backend.get()))
            if (auto action = watches->rules(snapshot))
              admitAction(entry, *action, snapshot);
        } catch (const std::exception &error) {
          diagnostics_.lastBackendError = error.what();
          failed(entry);
        } catch (...) {
          diagnostics_.lastBackendError = "debug backend inspection failed";
          failed(entry);
        }
      }
  }
  void publish(const TraceRecord &record) {
    lane();
    for (auto &entry : entries_)
      if (entry.enabled)
        if (auto *sink =
                dynamic_cast<ITraceSinkPluginV1 *>(entry.backend.get())) {
          try {
            if (!sink->consume(record)) {
              diagnostics_.lastBackendError = "trace sink disconnected";
              failed(entry);
            }
          } catch (const std::exception &error) {
            diagnostics_.lastBackendError = error.what();
            failed(entry);
          } catch (...) {
            diagnostics_.lastBackendError = "trace sink failed";
            failed(entry);
          }
        }
  }
  std::optional<Command> action() {
    lane();
    return actions_.pop();
  }
  const BackendDiagnostics &diagnostics() const {
    lane();
    return diagnostics_;
  }
};
} // namespace BMMQ::Debug
