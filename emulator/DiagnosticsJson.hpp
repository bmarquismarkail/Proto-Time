#ifndef BMMQ_EMULATOR_DIAGNOSTICS_JSON_HPP
#define BMMQ_EMULATOR_DIAGNOSTICS_JSON_HPP

#include <cstddef>
#include <ostream>

#include "machine/BackgroundTaskService.hpp"
#include "machine/TimingService.hpp"

namespace BMMQ {

inline void writeBackgroundTaskDiagnosticsJson(std::ostream& output,
                                               const BackgroundTaskStats& stats)
{
    output << "{\"worker_count\":" << stats.workerCount;
    output << ",\"submitted\":" << stats.tasksSubmitted;
    output << ",\"completed\":" << stats.tasksCompleted;
    output << ",\"pending\":" << stats.tasksPending;
    output << ",\"rejected\":" << stats.tasksRejected;
    output << ",\"cancelled\":" << stats.tasksCancelled;
    output << ",\"task_failures\":" << stats.taskFailures;
    output << ",\"high_water_pending\":" << stats.tasksHighWaterPending;
    output << ",\"steal_attempts\":" << stats.stealAttempts;
    output << ",\"steals_succeeded\":" << stats.stealsSucceeded;
    output << ",\"categories\":{";
    for (std::size_t index = 0; index < stats.categories.size(); ++index) {
        if (index != 0u) output << ',';
        const auto category = static_cast<BackgroundJobCategory>(index);
        const auto& categoryStats = stats.categories[index];
        output << '\"' << backgroundJobCategoryName(category) << "\":{";
        output << "\"submitted\":" << categoryStats.submitted;
        output << ",\"completed\":" << categoryStats.completed;
        output << ",\"rejected\":" << categoryStats.rejected;
        output << ",\"cancelled\":" << categoryStats.cancelled;
        output << ",\"queue_wait_total_ns\":" << categoryStats.queueWaitTotalNanos;
        output << ",\"queue_wait_high_water_ns\":" << categoryStats.queueWaitHighWaterNanos;
        output << ",\"execution_total_ns\":" << categoryStats.executionTotalNanos;
        output << ",\"execution_high_water_ns\":" << categoryStats.executionHighWaterNanos;
        output << '}';
    }
    output << "}}";
}

inline void writeTimingDiagnosticsJson(std::ostream& output, const TimingStats& stats)
{
    output << "{\"cycle_budget\":" << stats.cycleBudget;
    output << ",\"cycle_debt\":" << stats.cycleDebt;
    output << ",\"effective_clock_hz\":" << stats.effectiveClockHz;
    output << ",\"wake_sleeps\":" << stats.wakeSleeps;
    output << ",\"catch_up_clamps\":" << stats.catchUpClampCount;
    output << ",\"idle_loops\":" << stats.idleLoops;
    output << ",\"sleep_decisions\":" << stats.sleepDecisions;
    output << ",\"sleep_skipped_small_deficit\":" << stats.sleepSkippedForSmallDeficit;
    output << ",\"configured_min_sleep_quantum_ns\":"
           << stats.configuredMinSleepQuantum.count();
    output << ",\"execution_slices_entered\":" << stats.executionSlicesEntered;
    output << ",\"execution_slices_completed\":" << stats.executionSlicesCompleted;
    output << ",\"frontend_service_checks\":" << stats.frontendServiceChecks;
    output << ",\"last_execution_slice_cycles\":" << stats.lastExecutionSliceCycles;
    output << ",\"current_execution_slice_cycles\":" << stats.currentExecutionSliceCycles;
    output << ",\"frontend_ticks_scheduled\":" << stats.frontendTicksScheduled;
    output << ",\"frontend_ticks_executed\":" << stats.frontendTicksExecuted;
    output << ",\"frontend_ticks_merged\":" << stats.frontendTicksMerged;
    output << ",\"frontend_tick_delay_last_ns\":" << stats.frontendTickDelayLast.count();
    output << ",\"frontend_tick_delay_high_water_ns\":" << stats.frontendTickDelayHighWater.count();
    output << ",\"sleep_calls\":" << stats.sleepCalls;
    output << ",\"sleep_wake_early\":" << stats.sleepWakeEarlyCount;
    output << ",\"sleep_wake_late\":" << stats.sleepWakeLateCount;
    output << ",\"wake_jitter_under_100us\":" << stats.sleepWakeJitterUnder100usCount;
    output << ",\"wake_jitter_100_to_500us\":" << stats.sleepWakeJitter100To500usCount;
    output << ",\"wake_jitter_500us_to_2ms\":" << stats.sleepWakeJitter500usTo2msCount;
    output << ",\"wake_jitter_over_2ms\":" << stats.sleepWakeJitterOver2msCount;
    output << ",\"late_streak_current\":" << stats.sleepWakeLateStreakCurrent;
    output << ",\"late_streak_high_water\":" << stats.sleepWakeLateStreakHighWater;
    output << ",\"sleep_overshoot_count\":" << stats.sleepOvershootCount;
    output << ",\"sleep_overshoot_last_ns\":" << stats.sleepOvershootLast.count();
    output << ",\"sleep_overshoot_high_water_ns\":" << stats.sleepOvershootHighWater.count();
    output << ",\"wake_burst_samples\":" << stats.wakeBurstSamples;
    output << ",\"wake_burst_slices_last\":" << stats.wakeBurstSlicesLast;
    output << ",\"wake_burst_slices_high_water\":" << stats.wakeBurstSlicesHighWater;
    output << ",\"wake_burst_cycles_last\":" << stats.wakeBurstCyclesLast;
    output << ",\"wake_burst_cycles_high_water\":" << stats.wakeBurstCyclesHighWater;
    output << ",\"wake_burst_slice_limit_hits\":" << stats.wakeBurstSliceLimitHitCount;
    output << ",\"wake_burst_cycle_limit_hits\":" << stats.wakeBurstCycleLimitHitCount;
    output << '}';
}

} // namespace BMMQ

#endif // BMMQ_EMULATOR_DIAGNOSTICS_JSON_HPP
