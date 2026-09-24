#pragma once
#include "ModDirectoryLoader.hpp"
#include "TimeModAbi.h"
#include <memory>

namespace BMMQ::Modding {
struct NativeModState {
    std::string id, version;
    std::uint32_t schema = 0;
    std::vector<std::uint8_t> bytes;
};

// Owns library and instance; shared host lifetime outlives destroy. Thread affine.
// Does not automatically install guest traps or execute during manifest parsing.
class NativeMod final {
public:
    static std::unique_ptr<NativeMod> load(const LoadedMod&, std::shared_ptr<ModHost>);
    static std::unique_ptr<NativeMod> load(const LoadedMod&, ModHost&);
    ~NativeMod();
    NativeMod(const NativeMod&) = delete;
    NativeMod& operator=(const NativeMod&) = delete;
    bool invoke(TimeModCallV1&);
    bool reset();
    bool observe(const TimeModObservationV1&);
    NativeModState save();
    bool restore(const NativeModState&);
private:
    struct Impl;
    NativeMod();
    std::unique_ptr<Impl> impl_;
};
}
