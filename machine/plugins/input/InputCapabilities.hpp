#ifndef BMMQ_INPUT_CAPABILITIES_HPP
#define BMMQ_INPUT_CAPABILITIES_HPP

namespace BMMQ {

// Declares which input paths and lifecycle modes an adapter can safely support.
struct InputPluginCapabilities {
    // True when the machine loop may poll the adapter directly without blocking or unsafe mutation.
    bool pollingSafe = false;
    // True when host event ingestion is safe while the adapter is active, e.g. SDL-style pumps.
    bool eventPumpSafe = false;
    // True when visible input state does not depend on wall-clock timing or nondeterministic host behavior.
    bool deterministic = false;
    // True when the plugin can provide logical digital button state such as d-pad or face buttons.
    bool supportsDigital = false;
    // True when the plugin can provide analog channels such as sticks or triggers.
    bool supportsAnalog = false;
    // True when the plugin uses a stable machine-owned control layout rather than backend-specific labels.
    bool fixedLogicalLayout = false;
    // True when the service may replace or rebind the adapter while active without restarting input flow.
    bool hotSwapSafe = false;
    // True when rewind/seek style input repositioning is supported during live execution, e.g. replay sources.
    bool liveSeek = false;
    // True when the adapter must stay off realtime/live paths; such plugins are rejected by realtime engines.
    bool nonRealtimeOnly = false;
    // True when the adapter remains usable without a windowing or controller backend, e.g. replay or CLI input.
    bool headlessSafe = true;
};

} // namespace BMMQ

#endif // BMMQ_INPUT_CAPABILITIES_HPP