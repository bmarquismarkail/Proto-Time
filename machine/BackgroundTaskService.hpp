#ifndef BMMQ_BACKGROUND_TASK_SERVICE_HPP
#define BMMQ_BACKGROUND_TASK_SERVICE_HPP

#include <cstddef>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "machine/BackgroundThreadPool.hpp"

namespace BMMQ {

struct BackgroundTaskStats {
    std::size_t workerCount = 0;
    std::size_t tasksSubmitted = 0;
    std::size_t tasksCompleted = 0;
    std::size_t tasksPending = 0;
    std::size_t taskFailures = 0;
    std::size_t tasksRejected = 0;
    std::size_t tasksHighWaterPending = 0;
    std::size_t tasksCancelled = 0;
    std::size_t stealAttempts = 0;
    std::size_t stealsSucceeded = 0;
    std::array<BackgroundJobStats, kBackgroundJobCategoryCount> categories{};
};

// Backward-compatible wrapper around BackgroundThreadPool.
// Existing code that uses BackgroundTaskService continues to work;
// the underlying implementation is now a work-stealing thread pool.
class BackgroundTaskService final {
public:
    static constexpr std::size_t kDefaultMaxQueuedTasks = 1024u;

    [[nodiscard]] static std::size_t defaultWorkerCount() noexcept
    {
        const auto hardwareThreads = std::max<std::size_t>(1u, std::thread::hardware_concurrency());
        const auto available = hardwareThreads > 3u ? hardwareThreads - 3u : 1u;
        return std::clamp<std::size_t>(available, 1u, 8u);
    }

    explicit BackgroundTaskService(
        std::optional<std::size_t> maxQueuedTasks = std::nullopt,
        std::optional<std::size_t> threadCount = std::nullopt) noexcept
    {
        const auto cap = maxQueuedTasks.value_or(kDefaultMaxQueuedTasks);
        pool_ = std::make_unique<BackgroundThreadPool>(
            threadCount.value_or(defaultWorkerCount()),
            BackgroundThreadPool::kDefaultMaxQueuedTasksPerWorker,
            cap == 0u ? 1u : cap);
    }

    BackgroundTaskService(const BackgroundTaskService&) = delete;
    BackgroundTaskService& operator=(const BackgroundTaskService&) = delete;

    ~BackgroundTaskService()
    {
        shutdown();
    }

    void start()
    {
        pool_->start();
        running_.store(true, std::memory_order_release);
    }

    void shutdown()
    {
        if (pool_) {
            pool_->shutdown();
        }
        running_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool submit(std::function<void()> task)
    {
        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }
        return pool_ ? pool_->submit(std::move(task)) : false;
    }

    [[nodiscard]] bool submit(BackgroundJobCategory category, std::function<void()> task)
    {
        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }
        return pool_ ? pool_->submit(category, std::move(task)) : false;
    }

    [[nodiscard]] bool waitUntilIdle(std::chrono::milliseconds timeout) const
    {
        return pool_ != nullptr && pool_->waitUntilIdle(timeout);
    }

    [[nodiscard]] BackgroundTaskStats stats() const noexcept
    {
        if (!pool_) {
            return {};
        }
        auto s = pool_->stats();
        return BackgroundTaskStats{
            .workerCount = s.workerCount,
            .tasksSubmitted = s.tasksSubmitted,
            .tasksCompleted = s.tasksCompleted,
            .tasksPending = s.tasksPending,
            .taskFailures = s.taskFailures,
            .tasksRejected = s.tasksRejected,
            .tasksHighWaterPending = s.tasksHighWaterPending,
            .tasksCancelled = s.tasksCancelled,
            .stealAttempts = s.stealAttempts,
            .stealsSucceeded = s.stealsSucceeded,
            .categories = s.categories,
        };
    }

    // Cancel all queued tasks that have not yet started executing.
    // Returns the number of tasks removed from queues.
    std::size_t cancelAll()
    {
        if (!pool_) {
            return 0;
        }
        return pool_->cancelAll();
    }

private:
    std::unique_ptr<BackgroundThreadPool> pool_;
    std::atomic<bool> running_{false};
};

} // namespace BMMQ

#endif // BMMQ_BACKGROUND_TASK_SERVICE_HPP
