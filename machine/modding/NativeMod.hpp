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
    void setExecutionBinding(std::string binding);
    [[nodiscard]] bool attachedTo(const ModHost&) const noexcept;
    class PreparedCohort final {
    public:
        ~PreparedCohort();
        PreparedCohort(PreparedCohort&&) noexcept;
        PreparedCohort& operator=(PreparedCohort&&) noexcept;
        void commit();
    private:
        friend class NativeMod;
        struct State;
        explicit PreparedCohort(std::unique_ptr<State>);
        std::unique_ptr<State> state_;
    };
    // Includes observer-only modules loaded against this host. Machine-lane only.
    [[nodiscard]] static bool hasCohort(const ModHost&);
    static std::vector<std::uint8_t> saveCohort(const ModHost&);
    static PreparedCohort prepareCohort(ModHost&,
        std::optional<std::span<const std::uint8_t>> modules,
        std::optional<std::span<const std::uint8_t>> regions);
    static PreparedCohort prepareReset(ModHost&);
private:
    struct Impl;
    NativeMod();
    static std::unique_ptr<NativeMod> loadImpl(const LoadedMod&, std::shared_ptr<ModHost>, bool);
    std::unique_ptr<Impl> impl_;
};
}
