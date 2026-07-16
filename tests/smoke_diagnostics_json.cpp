#include <cassert>
#include <chrono>
#include <sstream>
#include <string>

#include "emulator/DiagnosticsJson.hpp"

int main()
{
    BMMQ::TimingStats timing;
    timing.frontendTicksScheduled = 11;
    timing.frontendTicksExecuted = 12;
    timing.frontendTicksMerged = 13;
    timing.sleepCalls = 21;
    timing.sleepWakeJitterOver2msCount = 22;
    timing.sleepOvershootLast = std::chrono::nanoseconds(23);
    timing.wakeBurstSliceLimitHitCount = 24;
    timing.wakeBurstCycleLimitHitCount = 25;

    std::ostringstream timingJson;
    BMMQ::writeTimingDiagnosticsJson(timingJson, timing);
    const auto timingText = timingJson.str();
    assert(timingText.starts_with('{') && timingText.ends_with('}'));
    assert(timingText.find("\"frontend_ticks_scheduled\":11") != std::string::npos);
    assert(timingText.find("\"frontend_ticks_executed\":12") != std::string::npos);
    assert(timingText.find("\"frontend_ticks_merged\":13") != std::string::npos);
    assert(timingText.find("\"sleep_calls\":21") != std::string::npos);
    assert(timingText.find("\"wake_jitter_over_2ms\":22") != std::string::npos);
    assert(timingText.find("\"sleep_overshoot_last_ns\":23") != std::string::npos);
    assert(timingText.find("\"wake_burst_slice_limit_hits\":24") != std::string::npos);
    assert(timingText.find("\"wake_burst_cycle_limit_hits\":25") != std::string::npos);

    BMMQ::BackgroundTaskStats background;
    background.workerCount = 3;
    background.taskFailures = 31;
    background.stealAttempts = 32;
    background.stealsSucceeded = 33;

    std::ostringstream backgroundJson;
    BMMQ::writeBackgroundTaskDiagnosticsJson(backgroundJson, background);
    const auto backgroundText = backgroundJson.str();
    assert(backgroundText.starts_with('{') && backgroundText.ends_with('}'));
    assert(backgroundText.find("\"worker_count\":3") != std::string::npos);
    assert(backgroundText.find("\"task_failures\":31") != std::string::npos);
    assert(backgroundText.find("\"steal_attempts\":32") != std::string::npos);
    assert(backgroundText.find("\"steals_succeeded\":33") != std::string::npos);
    assert(backgroundText.find("\"categories\":{") != std::string::npos);
}
