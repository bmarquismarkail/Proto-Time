#ifndef BMMQ_BACKGROUND_THREAD_POOL_HPP
#define BMMQ_BACKGROUND_THREAD_POOL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace BMMQ {

// Lightweight statistics mirror for the new thread pool.
struct BackgroundThreadPoolStats {
    std::size_t tasksSubmitted = 0;
    std::size_t tasksCompleted = 0;
    std::size_t tasksPending = 0;
    std::size_t taskFailures = 0;
    std::size_t tasksRejected = 0;
    std::size_t stealAttempts = 0;
    std::size_t stealsSucceeded = 0;
    std::size_t tasksHighWaterPending = 0;
    std::size_t tasksCancelled = 0;
};

// A work-stealing thread pool with one worker per core.
// Each worker owns a local deque and can steal from other workers' deques.
class BackgroundThreadPool final {
public:
    static constexpr std::size_t kDefaultMaxQueuedTasksPerWorker = 1024u;

    explicit BackgroundThreadPool(
        std::optional<std::size_t> threadCount = std::nullopt,
        std::size_t maxQueuedTasksPerWorker = kDefaultMaxQueuedTasksPerWorker) noexcept
        : threadCount_(std::max<std::size_t>(
              1u,
              threadCount.value_or(std::max<std::size_t>(1u, std::thread::hardware_concurrency())))),
          maxQueuedTasksPerWorker_(maxQueuedTasksPerWorker == 0u ? 1u : maxQueuedTasksPerWorker)
    {
        // Reserve storage for workers.
        workerLocal_.reserve(threadCount_);
        for (std::size_t i = 0; i < threadCount_; ++i) {
            auto ws = std::make_unique<WorkerState>();
            ws->id = i;
            workerLocal_.push_back(std::move(ws));
        }
    }

    BackgroundThreadPool(const BackgroundThreadPool&) = delete;
    BackgroundThreadPool& operator=(const BackgroundThreadPool&) = delete;

    ~BackgroundThreadPool()
    {
        shutdown();
    }

    void start()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (running_.load(std::memory_order_acquire)) {
            return;
        }
        stopRequested_.store(false, std::memory_order_release);
        cancelled_ = 0;
        workers_.clear();
        workers_.reserve(threadCount_);
        for (std::size_t i = 0; i < threadCount_; ++i) {
            workers_.emplace_back([this, i]() { workerLoop(i); });
        }
        running_.store(true, std::memory_order_release);
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex_);
            if (!running_.load(std::memory_order_acquire)) {
                return;
            }
            stopRequested_.store(true, std::memory_order_release);
        }
        notifyAllWorkers();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        running_.store(false, std::memory_order_release);
        workers_.clear();
    }

    // Cancel all queued tasks that have not yet started executing.
    // Tasks already in execution will complete normally.
    // Returns the number of tasks removed from queues.
    std::size_t cancelAll()
    {
        std::size_t cancelled = 0;
        for (std::size_t i = 0; i < workerLocal_.size(); ++i) {
            WorkerState& ws = *workerLocal_[i];
            std::lock_guard<std::mutex> lock(ws.mutex);
            cancelled += ws.queue.size();
            tasksPending_.fetch_sub(ws.queue.size(), std::memory_order_relaxed);
            ws.queue.clear();
        }
        if (cancelled > 0) {
            cancelled_.fetch_add(cancelled, std::memory_order_relaxed);
        }
        return cancelled;
    }

    // Submit a task to the pool. The pool uses round-robin load-balancing
    // to distribute submissions across workers. Returns false if the pool
    // is shut down or all worker queues are full.
    [[nodiscard]] bool submit(std::function<void()> task)
    {
        std::size_t queuedWorker = workerLocal_.size();
        {
            std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
            if (!running_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire)) {
                tasksRejected_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            const std::size_t rr = roundRobin_.fetch_add(1, std::memory_order_relaxed);
            for (std::size_t offset = 0; offset < workerLocal_.size(); ++offset) {
                const std::size_t idx = (rr + offset) % workerLocal_.size();
                WorkerState& ws = *workerLocal_[idx];

                std::lock_guard<std::mutex> lock(ws.mutex);
                if (ws.queue.size() >= maxQueuedTasksPerWorker_) {
                    continue;
                }

                ws.queue.push_back(std::move(task));
                queuedWorker = idx;
                tasksSubmitted_.fetch_add(1, std::memory_order_relaxed);
                const auto pending = tasksPending_.fetch_add(1, std::memory_order_relaxed) + 1u;
                updateHighWater(pending);
                break;
            }
        }

        if (queuedWorker != workerLocal_.size()) {
            workerLocal_[queuedWorker]->cv.notify_one();
            notifyAllWorkers();
            return true;
        }

        tasksRejected_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] BackgroundThreadPoolStats stats() const noexcept
    {
        const auto submitted = tasksSubmitted_.load(std::memory_order_acquire);
        const auto completed = tasksCompleted_.load(std::memory_order_acquire);
        const auto cancelled = cancelled_.load(std::memory_order_acquire);
        const auto accounted = completed + cancelled;
        const auto pending = submitted >= accounted ? submitted - accounted : 0u;
        return BackgroundThreadPoolStats{
            submitted,
            completed,
            pending,
            taskFailures_.load(std::memory_order_relaxed),
            tasksRejected_.load(std::memory_order_relaxed),
            stealAttempts_.load(std::memory_order_relaxed),
            stealSucceeded_.load(std::memory_order_relaxed),
            tasksHighWaterPending_.load(std::memory_order_relaxed),
            cancelled,
        };
    }

private:
    struct WorkerState {
        std::size_t id = 0;
        std::mutex mutex{};
        std::condition_variable cv{};
        std::deque<std::function<void()>> queue{};
    };

    void updateHighWater(std::size_t current) noexcept
    {
        auto hw = tasksHighWaterPending_.load(std::memory_order_relaxed);
        while (current > hw &&
               !tasksHighWaterPending_.compare_exchange_weak(
                   hw, current, std::memory_order_relaxed, std::memory_order_relaxed)) {
            // retry
        }
    }

    void workerLoop(std::size_t myId)
    {
        while (true) {
            if (auto task = takeLocalTask(myId); task.has_value()) {
                executeTask(std::move(*task));
                continue;
            }

            if (auto task = stealTask(myId); task.has_value()) {
                executeTask(std::move(*task));
                continue;
            }

            if (stopRequested_.load(std::memory_order_acquire) && !hasQueuedWork()) {
                break;
            }

            std::unique_lock<std::mutex> lock(workerLocal_[myId]->mutex);
            workerLocal_[myId]->cv.wait_for(lock, std::chrono::milliseconds(1), [this, myId]() {
                return stopRequested_.load(std::memory_order_acquire) ||
                       !workerLocal_[myId]->queue.empty() ||
                       tasksPending_.load(std::memory_order_relaxed) > 0u;
            });
        }
    }

    void executeTask(std::function<void()> task)
    {
        try {
            task();
        } catch (...) {
            taskFailures_.fetch_add(1, std::memory_order_relaxed);
        }
        tasksCompleted_.fetch_add(1, std::memory_order_relaxed);
        tasksPending_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] std::optional<std::function<void()>> takeLocalTask(std::size_t myId)
    {
        WorkerState& ws = *workerLocal_[myId];
        std::lock_guard<std::mutex> lock(ws.mutex);
        if (ws.queue.empty()) {
            return std::nullopt;
        }

        auto task = std::move(ws.queue.front());
        ws.queue.pop_front();
        return task;
    }

    [[nodiscard]] std::optional<std::function<void()>> stealTask(std::size_t myId)
    {
        for (std::size_t offset = 0; offset < workerLocal_.size(); ++offset) {
            const std::size_t victimIdx = (myId + 1u + offset) % workerLocal_.size();
            if (victimIdx == myId) {
                continue;
            }

            WorkerState& victim = *workerLocal_[victimIdx];
            std::lock_guard<std::mutex> lock(victim.mutex);
            stealAttempts_.fetch_add(1, std::memory_order_relaxed);
            if (!victim.queue.empty()) {
                auto stolen = std::move(victim.queue.back());
                victim.queue.pop_back();
                stealSucceeded_.fetch_add(1, std::memory_order_relaxed);
                return stolen;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool hasQueuedWork() const
    {
        for (const auto& worker : workerLocal_) {
            std::lock_guard<std::mutex> lock(worker->mutex);
            if (!worker->queue.empty()) {
                return true;
            }
        }
        return false;
    }

    void notifyAllWorkers() noexcept
    {
        for (auto& worker : workerLocal_) {
            worker->cv.notify_one();
        }
    }

    mutable std::mutex lifecycleMutex_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    const std::size_t threadCount_;
    const std::size_t maxQueuedTasksPerWorker_;

    std::atomic<std::size_t> roundRobin_{0};

    // Worker states — one per logical core.
    std::vector<std::unique_ptr<WorkerState>> workerLocal_{};
    std::vector<std::thread> workers_{};

    // Global counters.
    std::atomic<std::size_t> tasksSubmitted_{0};
    std::atomic<std::size_t> tasksCompleted_{0};
    std::atomic<std::size_t> tasksPending_{0};
    std::atomic<std::size_t> taskFailures_{0};
    std::atomic<std::size_t> tasksRejected_{0};
    std::atomic<std::size_t> stealAttempts_{0};
    std::atomic<std::size_t> stealSucceeded_{0};
    std::atomic<std::size_t> tasksHighWaterPending_{0};
    std::atomic<std::size_t> cancelled_{0};
};

} // namespace BMMQ

#endif // BMMQ_BACKGROUND_THREAD_POOL_HPP
