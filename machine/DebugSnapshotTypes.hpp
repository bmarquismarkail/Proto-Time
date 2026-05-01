#pragma once

#include <cstddef>
#include <cstdint>

namespace BMMQ {

/// Aggregate counters reported by DebugSnapshotService.
struct DebugSnapshotStats {
    /// Total video captures submitted (emulation thread).
    std::size_t videoSubmissions = 0;
    /// Total video captures successfully consumed (render thread).
    std::size_t videoConsumptions = 0;
    /// Video submits that were dropped because the queue was at capacity.
    std::size_t videoOverflows = 0;

    /// Total audio captures submitted (emulation thread).
    std::size_t audioSubmissions = 0;
    /// Total audio captures successfully consumed (render thread).
    std::size_t audioConsumptions = 0;
    /// Audio submits that were dropped because the queue was at capacity.
    std::size_t audioOverflows = 0;
};

} // namespace BMMQ
