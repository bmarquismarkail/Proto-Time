#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "machine/BackgroundTaskService.hpp"

namespace {

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

} // namespace

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
    // Use a single-worker pool for deterministic bounded-queue testing.
    BMMQ::BackgroundTaskService boundedService(4u, 1u);
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

    // Fill the single worker's queue while the first task is pinned in flight.
    for (int i = 0; i < 3; ++i) {
        assert(boundedService.submit([]() {}));
    }
    // One of these submissions should be rejected once the queue reaches capacity 4.
    bool gotRejection = false;
    for (int i = 0; i < 5; ++i) {
        if (!boundedService.submit([]() {})) {
            gotRejection = true;
            break;
        }
    }
    assert(gotRejection);

    {
        std::lock_guard<std::mutex> lock(gateMutex);
        allowFirstTaskToFinish = true;
    }
    gateCv.notify_all();

    boundedService.shutdown();
    const auto boundedStats = boundedService.stats();
    assert(boundedStats.tasksSubmitted == 5u);
    assert(boundedStats.tasksRejected >= 1u);
    assert(boundedStats.tasksCompleted == boundedStats.tasksSubmitted);
    assert(boundedStats.tasksPending == 0u);
    assert(boundedStats.tasksHighWaterPending >= 4u);

    // Preserve BackgroundTaskService's total queue cap when backed by a multi-worker pool.
    BMMQ::BackgroundTaskService globalBoundedService(2u, 2u);
    globalBoundedService.start();

    std::mutex globalGateMutex;
    std::condition_variable globalGateCv;
    bool allowGlobalBlockers = false;
    std::atomic<std::size_t> globalBlockersRunning{0};

    for (std::size_t i = 0; i < 2u; ++i) {
        const bool queued = globalBoundedService.submit([&]() {
            globalBlockersRunning.fetch_add(1u, std::memory_order_release);
            std::unique_lock<std::mutex> lock(globalGateMutex);
            globalGateCv.wait(lock, [&allowGlobalBlockers]() {
                return allowGlobalBlockers;
            });
        });
        assert(queued);
    }

    assert(waitUntil([&]() {
        return globalBlockersRunning.load(std::memory_order_acquire) == 2u;
    }, std::chrono::seconds(2)));

    assert(globalBoundedService.submit([]() {}));
    assert(globalBoundedService.submit([]() {}));
    assert(!globalBoundedService.submit([]() {}));

    {
        std::lock_guard<std::mutex> lock(globalGateMutex);
        allowGlobalBlockers = true;
    }
    globalGateCv.notify_all();

    globalBoundedService.shutdown();
    const auto globalBoundedStats = globalBoundedService.stats();
    assert(globalBoundedStats.tasksSubmitted == 4u);
    assert(globalBoundedStats.tasksRejected >= 1u);
    assert(globalBoundedStats.tasksCompleted == globalBoundedStats.tasksSubmitted);
    assert(globalBoundedStats.tasksPending == 0u);

    return 0;
}
