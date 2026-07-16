#ifndef BMMQ_TESTS_PERF_TIMING_SUPPORT_HPP
#define BMMQ_TESTS_PERF_TIMING_SUPPORT_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace BMMQ::Tests::Perf {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

#if defined(BMMQ_TSAN_ENABLED) || defined(__SANITIZE_THREAD__)
constexpr bool kEnforceWallClockThresholds = false;
#else
constexpr bool kEnforceWallClockThresholds = true;
#endif

[[nodiscard]] inline std::int64_t percentile(std::vector<std::int64_t> samples, double quantile)
{
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto rank = static_cast<std::size_t>(
        std::max(1.0, std::ceil(quantile * static_cast<double>(samples.size()))));
    return samples[std::min(rank - 1u, samples.size() - 1u)];
}

} // namespace BMMQ::Tests::Perf

#endif
