#ifndef BMMQ_BACKGROUND_THREAD_POOL_HPP
#define BMMQ_BACKGROUND_THREAD_POOL_HPP

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace BMMQ {

enum class BackgroundJobCategory : std::uint8_t {
    Generic = 0,
    VisualDecode,
    VisualReload,
    VisualCapture,
    SaveFlush,
    DebugSnapshot,
    VideoCapture,
    Count,
};

inline constexpr std::size_t kBackgroundJobCategoryCount =
    static_cast<std::size_t>(BackgroundJobCategory::Count);

[[nodiscard]] inline constexpr const char* backgroundJobCategoryName(
    BackgroundJobCategory category) noexcept
{
    switch (category) {
    case BackgroundJobCategory::VisualDecode: return "visual_decode";
    case BackgroundJobCategory::VisualReload: return "visual_reload";
    case BackgroundJobCategory::VisualCapture: return "visual_capture";
    case BackgroundJobCategory::SaveFlush: return "save_flush";
    case BackgroundJobCategory::DebugSnapshot: return "debug_snapshot";
    case BackgroundJobCategory::VideoCapture: return "video_capture";
    case BackgroundJobCategory::Generic:
    default: return "generic";
    }
}

struct BackgroundJobStats {
    std::size_t submitted = 0;
    std::size_t completed = 0;
    std::size_t rejected = 0;
    std::size_t cancelled = 0;
    std::uint64_t queueWaitTotalNanos = 0;
    std::uint64_t queueWaitHighWaterNanos = 0;
    std::uint64_t executionTotalNanos = 0;
    std::uint64_t executionHighWaterNanos = 0;
};

// Lightweight statistics mirror for the new thread pool.
struct BackgroundThreadPoolStats {
    std::size_t workerCount = 0;
    std::size_t tasksSubmitted = 0;
    std::size_t tasksCompleted = 0;
    std::size_t tasksPending = 0;
    std::size_t taskFailures = 0;
    std::size_t tasksRejected = 0;
    std::size_t stealAttempts = 0;
    std::size_t stealsSucceeded = 0;
    std::size_t tasksHighWaterPending = 0;
    std::size_t tasksCancelled = 0;
    std::array<BackgroundJobStats, kBackgroundJobCategoryCount> categories{};
};

// A bounded work-stealing thread pool. Production selects a reserved-core
// default; tests and hosts may provide an explicit worker count.
// Each worker owns a local deque and can steal from other workers' deques.
class BackgroundThreadPool final {
public:
    static constexpr std::size_t kDefaultMaxQueuedTasksPerWorker = 1024u;

    explicit BackgroundThreadPool(
        std::optional<std::size_t> threadCount = std::nullopt,
        std::size_t maxQueuedTasksPerWorker = kDefaultMaxQueuedTasksPerWorker,
        std::optional<std::size_t> maxQueuedTasksTotal = std::nullopt) noexcept
        : threadCount_(std::max<std::size_t>(
              1u,
              threadCount.value_or(std::max<std::size_t>(1u, std::thread::hardware_concurrency())))),
          maxQueuedTasksPerWorker_(maxQueuedTasksPerWorker == 0u ? 1u : maxQueuedTasksPerWorker),
          maxQueuedTasksTotal_(maxQueuedTasksTotal.value_or(0u))
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
            for (const auto& task : ws.queue) {
                categoryCancelled_[normalizedCategoryIndex(task.category)].fetch_add(
                    1u, std::memory_order_relaxed);
            }
            cancelled += ws.queue.size();
            tasksPending_.fetch_sub(ws.queue.size(), std::memory_order_relaxed);
            releaseQueuedSlots(ws.queue.size());
            ws.queue.clear();
        }
        if (cancelled > 0) {
            cancelled_.fetch_add(cancelled, std::memory_order_relaxed);
            if (tasksPending_.load(std::memory_order_acquire) == 0u) {
                idleCv_.notify_all();
            }
        }
        return cancelled;
    }

    // Submit a task to the pool. The pool uses round-robin load-balancing
    // to distribute submissions across workers. Returns false if the pool
    // is shut down, a configured queue cap is reached, or all worker queues are full.
    [[nodiscard]] bool submit(std::function<void()> task)
    {
        return submit(BackgroundJobCategory::Generic, std::move(task));
    }

    [[nodiscard]] bool submit(BackgroundJobCategory category, std::function<void()> task)
    {
        const auto categoryIndex = normalizedCategoryIndex(category);
        std::size_t queuedWorker = workerLocal_.size();
        bool queuedSlotReserved = false;
        {
            std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
            if (!running_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire)) {
                tasksRejected_.fetch_add(1, std::memory_order_relaxed);
                categoryRejected_[categoryIndex].fetch_add(1u, std::memory_order_relaxed);
                return false;
            }

            queuedSlotReserved = reserveQueuedSlot();
            if (!queuedSlotReserved) {
                tasksRejected_.fetch_add(1, std::memory_order_relaxed);
                categoryRejected_[categoryIndex].fetch_add(1u, std::memory_order_relaxed);
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

                ws.queue.push_back(TaskItem{
                    .function = std::move(task),
                    .category = category,
                    .queuedAt = std::chrono::steady_clock::now(),
                });
                queuedWorker = idx;
                tasksSubmitted_.fetch_add(1, std::memory_order_relaxed);
                categorySubmitted_[categoryIndex].fetch_add(1u, std::memory_order_relaxed);
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

        if (queuedSlotReserved) {
            releaseQueuedSlots(1u);
        }
        tasksRejected_.fetch_add(1, std::memory_order_relaxed);
        categoryRejected_[categoryIndex].fetch_add(1u, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] bool waitUntilIdle(std::chrono::milliseconds timeout) const
    {
        std::unique_lock<std::mutex> lock(idleMutex_);
        return idleCv_.wait_for(lock, timeout, [this]() {
            return tasksPending_.load(std::memory_order_acquire) == 0u;
        });
    }

    [[nodiscard]] BackgroundThreadPoolStats stats() const noexcept
    {
        const auto submitted = tasksSubmitted_.load(std::memory_order_acquire);
        const auto completed = tasksCompleted_.load(std::memory_order_acquire);
        const auto cancelled = cancelled_.load(std::memory_order_acquire);
        BackgroundThreadPoolStats result{};
        result.workerCount = threadCount_;
        result.tasksSubmitted = submitted;
        result.tasksCompleted = completed;
        result.tasksPending = tasksPending_.load(std::memory_order_acquire);
        result.taskFailures = taskFailures_.load(std::memory_order_relaxed);
        result.tasksRejected = tasksRejected_.load(std::memory_order_relaxed);
        result.stealAttempts = stealAttempts_.load(std::memory_order_relaxed);
        result.stealsSucceeded = stealSucceeded_.load(std::memory_order_relaxed);
        result.tasksHighWaterPending = tasksHighWaterPending_.load(std::memory_order_relaxed);
        result.tasksCancelled = cancelled;
        for (std::size_t i = 0; i < kBackgroundJobCategoryCount; ++i) {
            result.categories[i] = BackgroundJobStats{
                .submitted = categorySubmitted_[i].load(std::memory_order_relaxed),
                .completed = categoryCompleted_[i].load(std::memory_order_relaxed),
                .rejected = categoryRejected_[i].load(std::memory_order_relaxed),
                .cancelled = categoryCancelled_[i].load(std::memory_order_relaxed),
                .queueWaitTotalNanos = categoryQueueWaitTotalNanos_[i].load(std::memory_order_relaxed),
                .queueWaitHighWaterNanos = categoryQueueWaitHighWaterNanos_[i].load(std::memory_order_relaxed),
                .executionTotalNanos = categoryExecutionTotalNanos_[i].load(std::memory_order_relaxed),
                .executionHighWaterNanos = categoryExecutionHighWaterNanos_[i].load(std::memory_order_relaxed),
            };
        }
        return result;
    }

private:
    struct TaskItem {
        std::function<void()> function{};
        BackgroundJobCategory category = BackgroundJobCategory::Generic;
        std::chrono::steady_clock::time_point queuedAt{};
    };

    struct WorkerState {
        std::size_t id = 0;
        std::mutex mutex{};
        std::condition_variable cv{};
        std::deque<TaskItem> queue{};
    };

    [[nodiscard]] static constexpr std::size_t normalizedCategoryIndex(
        BackgroundJobCategory category) noexcept
    {
        const auto index = static_cast<std::size_t>(category);
        return index < kBackgroundJobCategoryCount ? index : 0u;
    }

    static void updateAtomicHighWater(std::atomic<std::uint64_t>& target,
                                      std::uint64_t current) noexcept
    {
        auto highWater = target.load(std::memory_order_relaxed);
        while (current > highWater &&
               !target.compare_exchange_weak(highWater, current,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
        }
    }

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
                       queuedTasks_.load(std::memory_order_relaxed) > 0u;
            });
        }
    }

    void executeTask(TaskItem task)
    {
        const auto categoryIndex = normalizedCategoryIndex(task.category);
        const auto startedAt = std::chrono::steady_clock::now();
        const auto queueWaitNanos = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(startedAt - task.queuedAt).count());
        categoryQueueWaitTotalNanos_[categoryIndex].fetch_add(queueWaitNanos, std::memory_order_relaxed);
        updateAtomicHighWater(categoryQueueWaitHighWaterNanos_[categoryIndex], queueWaitNanos);
        try {
            task.function();
        } catch (...) {
            taskFailures_.fetch_add(1, std::memory_order_relaxed);
        }
        const auto executionNanos = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - startedAt).count());
        categoryExecutionTotalNanos_[categoryIndex].fetch_add(executionNanos, std::memory_order_relaxed);
        updateAtomicHighWater(categoryExecutionHighWaterNanos_[categoryIndex], executionNanos);
        categoryCompleted_[categoryIndex].fetch_add(1u, std::memory_order_relaxed);
        tasksCompleted_.fetch_add(1, std::memory_order_relaxed);
        if (tasksPending_.fetch_sub(1, std::memory_order_acq_rel) == 1u) {
            idleCv_.notify_all();
        }
    }

    [[nodiscard]] std::optional<TaskItem> takeLocalTask(std::size_t myId)
    {
        WorkerState& ws = *workerLocal_[myId];
        std::lock_guard<std::mutex> lock(ws.mutex);
        if (ws.queue.empty()) {
            return std::nullopt;
        }

        auto task = std::move(ws.queue.front());
        ws.queue.pop_front();
        releaseQueuedSlots(1u);
        return task;
    }

    [[nodiscard]] std::optional<TaskItem> stealTask(std::size_t myId)
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
                releaseQueuedSlots(1u);
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

    [[nodiscard]] bool reserveQueuedSlot() noexcept
    {
        if (maxQueuedTasksTotal_ == 0u) {
            queuedTasks_.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }

        auto queued = queuedTasks_.load(std::memory_order_relaxed);
        while (queued < maxQueuedTasksTotal_) {
            if (queuedTasks_.compare_exchange_weak(
                    queued,
                    queued + 1u,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void releaseQueuedSlots(std::size_t count) noexcept
    {
        if (count == 0u) {
            return;
        }
        queuedTasks_.fetch_sub(count, std::memory_order_relaxed);
    }

    mutable std::mutex lifecycleMutex_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    const std::size_t threadCount_;
    const std::size_t maxQueuedTasksPerWorker_;
    const std::size_t maxQueuedTasksTotal_;

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
    std::atomic<std::size_t> queuedTasks_{0};
    mutable std::mutex idleMutex_{};
    mutable std::condition_variable idleCv_{};
    std::array<std::atomic<std::size_t>, kBackgroundJobCategoryCount> categorySubmitted_{};
    std::array<std::atomic<std::size_t>, kBackgroundJobCategoryCount> categoryCompleted_{};
    std::array<std::atomic<std::size_t>, kBackgroundJobCategoryCount> categoryRejected_{};
    std::array<std::atomic<std::size_t>, kBackgroundJobCategoryCount> categoryCancelled_{};
    std::array<std::atomic<std::uint64_t>, kBackgroundJobCategoryCount> categoryQueueWaitTotalNanos_{};
    std::array<std::atomic<std::uint64_t>, kBackgroundJobCategoryCount> categoryQueueWaitHighWaterNanos_{};
    std::array<std::atomic<std::uint64_t>, kBackgroundJobCategoryCount> categoryExecutionTotalNanos_{};
    std::array<std::atomic<std::uint64_t>, kBackgroundJobCategoryCount> categoryExecutionHighWaterNanos_{};
};

} // namespace BMMQ

#endif // BMMQ_BACKGROUND_THREAD_POOL_HPP
