#pragma once
#include "InputPlugin.hpp"

namespace BMMQ {
struct InputBoundarySnapshotV1 {
    InputButtonMask digital{};
    InputAnalogState analog{};
    bool neutralFallback{};
};
// Separately versioned host-input contract. The host event stream is not a
// deterministic replay source. This interface supplies one owned logical
// snapshot at a machine boundary; recording stores the committed snapshot.
class IInputSnapshotSourceV1 : public virtual IInputPlugin {
public:
    [[nodiscard]] virtual bool boundedBoundarySamplingV1() const noexcept = 0;
    [[nodiscard]] virtual InputBoundarySnapshotV1 sampleBoundaryV1(std::uint64_t generation) noexcept = 0;
};
}
