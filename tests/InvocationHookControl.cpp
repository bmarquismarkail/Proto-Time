#include "InvocationHookControl.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

// Separate translation unit prevents ordinary non-LTO compilation from folding
// the factory's returned hook pointers into the measured call site.
BMMQ::IR::Research::Hooks emptyHooks(EmptyHookState& state) {
  return {&state, &EmptyHookState::before, &EmptyHookState::retire};
}
std::uint64_t lookupPolicyBackend(const BMMQ::Machine& machine) {
  return static_cast<std::uint64_t>(machine.attachedExecutorPolicy().backend());
}
