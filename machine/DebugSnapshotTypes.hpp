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

    /// Video submissions accepted by the background task pool for deferred enqueue.
    std::size_t videoBackgroundSubmissions = 0;
    /// Video submissions that fell back because the background task pool rejected them.
    std::size_t videoBackgroundFallbacks = 0;
    /// Audio submissions accepted by the background task pool for deferred enqueue.
    std::size_t audioBackgroundSubmissions = 0;
    /// Audio submissions that fell back because the background task pool rejected them.
    std::size_t audioBackgroundFallbacks = 0;
};

} // namespace BMMQ
