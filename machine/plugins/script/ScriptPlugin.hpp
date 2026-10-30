#pragma once
#include "ScriptEngine.hpp"
#include "ScriptHookEngine.hpp"
#include "machine/plugins/IoPlugin.hpp"
namespace BMMQ::Script {
struct ScriptCapabilitiesV1 {
  bool deterministic{}, nonRealtimeOnly{true}, snapshotAware{true},
      machineMutationAllowed{}, headlessSafe{true}, boundedExecution{true};
};
// Separately versioned, owned-value contracts. Existing external ABIs are
// intact.
class IScriptingPluginV1 : public virtual BMMQ::IPlugin {
public:
  virtual Language language() const noexcept = 0;
  virtual ScriptCapabilitiesV1 capabilities() const noexcept = 0;
};
class IScriptAutomationPluginV1 : public virtual IScriptingPluginV1 {};
class IScriptHookPluginV1 : public virtual BMMQ::IPlugin {
public:
  virtual HookCapabilities hookCapabilities() const noexcept = 0;
  virtual std::span<const HookAction> preparedHooks() const noexcept = 0;
};
} // namespace BMMQ::Script
