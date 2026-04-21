#ifndef BMMQ_VIDEO_CAPABILITIES_HPP
#define BMMQ_VIDEO_CAPABILITIES_HPP

namespace BMMQ {

struct VideoPluginCapabilities {
    bool realtimeSafe = false;
    bool frameSizePreserving = false;
    bool snapshotAware = false;
    bool deterministic = false;
    bool headlessSafe = false;
    bool requiresHostThreadAffinity = false;
    bool nonRealtimeOnly = false;
    bool hotSwappable = false;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_CAPABILITIES_HPP
