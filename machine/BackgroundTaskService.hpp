#ifndef BMMQ_BACKGROUND_TASK_SERVICE_HPP
#define BMMQ_BACKGROUND_TASK_SERVICE_HPP

#include <cstddef>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "machine/BackgroundThreadPool.hpp"

namespace BMMQ {

struct BackgroundTaskStats {
    std::size_t tasksSubmitted = 0;
    std::size_t tasksCompleted = 0;
    std::size_t tasksPending = 0;
    std::size_t taskFailures = 0;
    std::size_t tasksRejected = 0;
    std::size_t tasksHighWaterPending = 0;
};

// Backward-compatible wrapper around BackgroundThreadPool.
// Existing code that uses BackgroundTaskService continues to work;
// the underlying implementation is now a work-stealing thread pool.
class BackgroundTaskService final {
public:
    static constexpr std::size_t kDefaultMaxQueuedTasks = 1024u;

    explicit BackgroundTaskService(
        std::optional<std::size_t> maxQueuedTasks = std::nullopt,
        std::optional<std::size_t> threadCount = std::nullopt) noexcept
    {
        auto cap = maxQueuedTasks.value_or(kDefaultMaxQueuedTasks);
        pool_ = std::make_unique<BackgroundThreadPool>(threadCount, cap);
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

    [[nodiscard]] BackgroundTaskStats stats() const noexcept
    {
        if (!pool_) {
            return {};
        }
        auto s = pool_->stats();
        return BackgroundTaskStats{
            s.tasksSubmitted,
            s.tasksCompleted,
            s.tasksPending,
            s.taskFailures,
            s.tasksRejected,
            s.tasksHighWaterPending,
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
