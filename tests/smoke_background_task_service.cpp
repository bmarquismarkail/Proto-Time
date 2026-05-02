#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "machine/BackgroundTaskService.hpp"

int main()
{
    BMMQ::BackgroundTaskService service;
    service.start();

    std::atomic<std::size_t> ran{0};
    constexpr std::size_t kTaskCount = 8u;
    for (std::size_t i = 0; i < kTaskCount; ++i) {
        const bool queued = service.submit([&ran]() {
            ran.fetch_add(1u, std::memory_order_relaxed);
        });
        assert(queued);
    }

    const auto tempRoot = std::filesystem::temp_directory_path() / "proto-time-background-task-smoke";
    std::filesystem::create_directories(tempRoot);
    const auto markerPath = tempRoot / "marker.txt";
    std::error_code ec;
    std::filesystem::remove(markerPath, ec);

    const bool markerQueued = service.submit([markerPath]() {
        std::ofstream output(markerPath, std::ios::binary | std::ios::trunc);
        assert(output);
        output << "ok";
        output.flush();
        assert(output.good());
    });
    assert(markerQueued);

    service.shutdown();

    assert(ran.load(std::memory_order_relaxed) == kTaskCount);
    const auto stats = service.stats();
    assert(stats.tasksSubmitted == kTaskCount + 1u);
    assert(stats.tasksCompleted == stats.tasksSubmitted);
    assert(stats.tasksPending == 0u);
    assert(stats.taskFailures == 0u);
    assert(std::filesystem::exists(markerPath));

    const bool queuedAfterShutdown = service.submit([]() {});
    assert(!queuedAfterShutdown);

    // Bounded queue behavior: reject on full queue and track pending high-water.
    BMMQ::BackgroundTaskService boundedService(2u);
    boundedService.start();

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool allowFirstTaskToFinish = false;
    std::atomic<bool> firstTaskRunning{false};

    const bool firstQueued = boundedService.submit([&]() {
        firstTaskRunning.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(gateMutex);
        gateCv.wait(lock, [&allowFirstTaskToFinish]() {
            return allowFirstTaskToFinish;
        });
    });
    assert(firstQueued);
    while (!firstTaskRunning.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    assert(boundedService.submit([]() {}));
    assert(boundedService.submit([]() {}));
    assert(!boundedService.submit([]() {}));

    {
        std::lock_guard<std::mutex> lock(gateMutex);
        allowFirstTaskToFinish = true;
    }
    gateCv.notify_all();

    boundedService.shutdown();
    const auto boundedStats = boundedService.stats();
    assert(boundedStats.tasksSubmitted == 3u);
    assert(boundedStats.tasksRejected >= 1u);
    assert(boundedStats.tasksCompleted == boundedStats.tasksSubmitted);
    assert(boundedStats.tasksPending == 0u);
    assert(boundedStats.tasksHighWaterPending >= 3u);

    return 0;
}
