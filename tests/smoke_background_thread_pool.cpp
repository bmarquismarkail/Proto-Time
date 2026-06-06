#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "machine/BackgroundThreadPool.hpp"

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
    // Test 1: Basic single-threaded pool with 2 tasks.
    {
        BMMQ::BackgroundThreadPool pool(1u);
        pool.start();

        std::atomic<std::size_t> counter{0};

        const bool ok1 = pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        assert(ok1);

        const bool ok2 = pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        assert(ok2);

        // Give tasks time to complete.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        pool.shutdown();

        assert(counter.load(std::memory_order_relaxed) == 2u);
    }

    // Test 2: Multi-threaded pool with concurrent submission.
    {
        constexpr std::size_t kThreadCount = 4u;
        constexpr std::size_t kTaskCount = 128u;
        BMMQ::BackgroundThreadPool pool(kThreadCount);
        pool.start();

        std::atomic<std::size_t> counter{0};

        for (std::size_t i = 0; i < kTaskCount; ++i) {
            const bool queued = pool.submit([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
            assert(queued);
        }

        // Wait for completion.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto s = pool.stats();
        assert(s.tasksCompleted == kTaskCount);
        assert(s.tasksPending == 0u);
        assert(s.tasksSubmitted == kTaskCount);
        assert(s.taskFailures == 0u);

        pool.shutdown();
    }

    // Test 3: Work stealing — idle workers steal queued work from a blocked owner.
    {
        constexpr std::size_t kThreadCount = 4u;
        BMMQ::BackgroundThreadPool pool(kThreadCount);
        pool.start();

        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool allowOwner = false;
        bool allowOthers = false;
        std::atomic<std::size_t> blockersRunning{0};
        std::atomic<bool> stolenMarkerRan{false};

        for (std::size_t i = 0; i < kThreadCount; ++i) {
            const bool ok = pool.submit([&, i]() {
                blockersRunning.fetch_add(1u, std::memory_order_release);
                std::unique_lock<std::mutex> lock(gateMutex);
               gateCv.wait(lock, [&]() { return i == 0u ? allowOwner : allowOthers; });
            });
            assert(ok);
        }

        assert(waitUntil([&]() {
            return blockersRunning.load(std::memory_order_acquire) == kThreadCount;
        }, std::chrono::seconds(2)));

        // Round-robin puts this task behind worker 0. It can run before owner release only by stealing.
        const bool markerQueued = pool.submit([&]() {
            stolenMarkerRan.store(true, std::memory_order_release);
        });
        assert(markerQueued);

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            allowOthers = true;
        }
        gateCv.notify_all();

        assert(waitUntil([&]() {
            return stolenMarkerRan.load(std::memory_order_acquire);
        }, std::chrono::seconds(2)));

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            allowOwner = true;
        }
        gateCv.notify_all();

        pool.shutdown();

        auto s = pool.stats();
        assert(s.tasksCompleted == kThreadCount + 1u);
        assert(s.stealsSucceeded >= 1u);
    }

    // Test 4: Task failure does not crash the worker.
    {
        BMMQ::BackgroundThreadPool pool(2u);
        pool.start();

        std::size_t goodCount = 0;
        const bool ok1 = pool.submit([&goodCount]() {
            ++goodCount;
        });
        assert(ok1);

        const bool ok2 = pool.submit([]() {
            throw std::runtime_error("boom");
        });
        assert(ok2);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        pool.shutdown();

        auto s = pool.stats();
        assert(s.tasksCompleted == 2u);
        assert(s.taskFailures == 1u);
        assert(goodCount == 1u);
    }

    // Test 5: Rejection when all queues are full.
    {
        constexpr std::size_t kThreadCount = 2u;
        constexpr std::size_t kMaxPerWorker = 4u;
        BMMQ::BackgroundThreadPool pool(kThreadCount, kMaxPerWorker);
        pool.start();

        // Block all workers by filling their queues.
        std::mutex gMutex;
        std::condition_variable gCv;
        bool allowAll = false;

        for (std::size_t i = 0; i < kThreadCount * kMaxPerWorker; ++i) {
            const bool ok = pool.submit([&]() {
                std::unique_lock<std::mutex> lock(gMutex);
               gCv.wait(lock, [&allowAll]() { return allowAll; });
            });
            assert(ok);
        }

        // One more should be rejected.
        const bool rejected = pool.submit([]() {});
        assert(!rejected);

        {
            std::lock_guard<std::mutex> lock(gMutex);
            allowAll = true;
        }
        gCv.notify_all();

        pool.shutdown();

        auto s = pool.stats();
        assert(s.tasksRejected >= 1u);
    }

    // Test 6: Stats consistency under concurrent load.
    {
        BMMQ::BackgroundThreadPool pool(4u);
        pool.start();

        std::atomic<std::size_t> counter{0};
        constexpr std::size_t kN = 256u;

        for (std::size_t i = 0; i < kN; ++i) {
            const bool ok = pool.submit([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
            assert(ok);
        }

        // Sample stats concurrently.
        std::atomic<bool> stopSampling{false};
        std::thread sampler([&pool, &stopSampling]() {
            while (!stopSampling.load(std::memory_order_relaxed)) {
               auto s = pool.stats();
               assert(s.tasksCompleted <= s.tasksSubmitted);
               assert(s.tasksPending == s.tasksSubmitted - s.tasksCompleted);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stopSampling.store(true, std::memory_order_relaxed);
        sampler.join();

        pool.shutdown();

        assert(counter.load(std::memory_order_relaxed) == kN);
    }

    // Test 7: cancelAll — queued tasks removed, in-flight tasks complete.
    {
        constexpr std::size_t kThreadCount = 1u;
        BMMQ::BackgroundThreadPool pool(kThreadCount);
        pool.start();

        std::mutex gMutex;
        std::condition_variable gCv;
        bool allowBlock{false};

        // Pin a long-running task to hold one worker.
        const bool pinned = pool.submit([&]() {
            std::unique_lock<std::mutex> lock(gMutex);
            gCv.wait(lock, [&allowBlock]() { return allowBlock; });
        });
        assert(pinned);

        // Submit 5 more tasks that should be cancellable.
        std::atomic<int> ranCount{0};
        for (int i = 0; i < 5; ++i) {
            const bool ok = pool.submit([&ranCount]() {
                ranCount.fetch_add(1, std::memory_order_relaxed);
            });
            assert(ok);
        }

        // Give tasks time to hit queues.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // Cancel queued tasks — at least the first worker's queue should have items.
        auto cancelled = pool.cancelAll();
        assert(cancelled >= 1u);  // at least one task was in a queue when cancelled
        assert(cancelled <= 6u);  // can't cancel more than total submitted

        auto midStats = pool.stats();
        assert(midStats.tasksCancelled >= 1u);

        // Unblock pinned task so shutdown can complete.
        {
            std::lock_guard<std::mutex> lock(gMutex);
            allowBlock = true;
        }
        gCv.notify_all();

        pool.shutdown();

        auto s = pool.stats();
        // Invariant: submitted == completed + pending + cancelled.
        assert(s.tasksCompleted + s.tasksPending + s.tasksCancelled == s.tasksSubmitted);
    }

    // Test 8: stopped pools reject submissions; cancelAll before start is a no-op.
    {
        BMMQ::BackgroundThreadPool pool(2u);
        assert(!pool.submit([]() {}));
        auto cancelled = pool.cancelAll();
        assert(cancelled == 0u);
        pool.start();
        pool.shutdown();
        assert(!pool.submit([]() {}));
        auto s = pool.stats();
        assert(s.tasksPending == 0u);
    }

    // Test 9: shutdown with queued work — workers drain remaining tasks.
    {
        BMMQ::BackgroundThreadPool pool(1u);
        pool.start();

        std::atomic<int> counter{0};
        for (int i = 0; i < 10; ++i) {
            const bool ok = pool.submit([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
            assert(ok);
        }

        // Shutdown without waiting — workers should drain.
        pool.shutdown();

        assert(counter.load(std::memory_order_relaxed) == 10);
    }

    // Test 10: Dependent task ordering — FIFO within a worker's queue.
    {
        BMMQ::BackgroundThreadPool pool(1u);  // single worker for deterministic ordering
        pool.start();

        std::vector<int> order;
        std::mutex orderMutex;

        // Submit tasks that record their execution order.
        for (int i = 0; i < 5; ++i) {
            const int expected = i;
            const bool ok = pool.submit([expected, &order, &orderMutex]() {
                std::lock_guard<std::mutex> lock(orderMutex);
                order.push_back(expected);
            });
            assert(ok);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pool.shutdown();

        // With single worker and FIFO, order should be 0,1,2,3,4.
        assert(order.size() == 5u);
        for (int i = 0; i < 5; ++i) {
            assert(order[i] == i);
        }
    }

    // Test 11: Work stealing does not starve shutdown.
    {
        constexpr std::size_t kThreadCount = 4u;
        BMMQ::BackgroundThreadPool pool(kThreadCount);
        pool.start();

        std::mutex gMutex;
        std::condition_variable gCv;
        bool allowBlock{false};

        // Fill all workers with long-running tasks.
        for (std::size_t i = 0; i < kThreadCount; ++i) {
            const bool ok = pool.submit([&]() {
                std::unique_lock<std::mutex> lock(gMutex);
               gCv.wait(lock, [&allowBlock]() { return allowBlock; });
            });
            assert(ok);
        }

        // Give workers time to pick up tasks and enter their CV waits.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Signal all blocking tasks to unblock so shutdown can complete cleanly.
        {
            std::lock_guard<std::mutex> lock(gMutex);
            allowBlock = true;
        }
        gCv.notify_all();

        // Shutdown — each worker should exit cleanly even while stealing from others.
        pool.shutdown();

        auto s = pool.stats();
        assert(s.tasksCompleted == kThreadCount);
    }

    // Test 12: Exception containment with cancellation.
    {
        BMMQ::BackgroundThreadPool pool(1u);
        pool.start();

        std::atomic<int> goodCount{0};
        std::mutex gMutex;
        std::condition_variable gCv;
        bool allowBlock{false};

        // Submit a blocking task.
        const bool pinned = pool.submit([&]() {
            std::unique_lock<std::mutex> lock(gMutex);
            gCv.wait(lock, [&allowBlock]() { return allowBlock; });
        });
        assert(pinned);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Submit a good task and an exception-throwing task.
        const bool okGood = pool.submit([&goodCount]() {
            goodCount.fetch_add(1, std::memory_order_relaxed);
        });
        assert(okGood);

        const bool okThrow = pool.submit([]() {
            throw std::runtime_error("boom");
        });
        assert(okThrow);

        // Cancel queued tasks (the good one and the throwing one).
        auto cancelled = pool.cancelAll();
        assert(cancelled >= 2u);

        auto s = pool.stats();
        assert(s.taskFailures == 0u);  // thrown task was cancelled, never ran
        assert(goodCount.load() == 0);  // good task was cancelled, never ran

        // Unblock pinned task.
        {
            std::lock_guard<std::mutex> lock(gMutex);
            allowBlock = true;
        }
        gCv.notify_all();

        pool.shutdown();
    }

    return 0;
}
