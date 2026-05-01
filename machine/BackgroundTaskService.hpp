#ifndef BMMQ_BACKGROUND_TASK_SERVICE_HPP
#define BMMQ_BACKGROUND_TASK_SERVICE_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace BMMQ {

struct BackgroundTaskStats {
    std::size_t tasksSubmitted = 0;
    std::size_t tasksCompleted = 0;
    std::size_t tasksPending = 0;
    std::size_t taskFailures = 0;
};

class BackgroundTaskService final {
public:
    BackgroundTaskService() = default;

    BackgroundTaskService(const BackgroundTaskService&) = delete;
    BackgroundTaskService& operator=(const BackgroundTaskService&) = delete;

    ~BackgroundTaskService()
    {
        shutdown();
    }

    void start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        stopRequested_ = false;
        worker_ = std::thread([this]() { workerLoop(); });
        running_ = true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }
            stopRequested_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }

    [[nodiscard]] bool submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || stopRequested_) {
                return false;
            }
            queue_.push_back(std::move(task));
            tasksSubmitted_.fetch_add(1u, std::memory_order_relaxed);
            tasksPending_.fetch_add(1u, std::memory_order_relaxed);
        }
        cv_.notify_one();
        return true;
    }

    [[nodiscard]] BackgroundTaskStats stats() const noexcept
    {
        return BackgroundTaskStats{
            tasksSubmitted_.load(std::memory_order_relaxed),
            tasksCompleted_.load(std::memory_order_relaxed),
            tasksPending_.load(std::memory_order_relaxed),
            taskFailures_.load(std::memory_order_relaxed),
        };
    }

private:
    void workerLoop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return stopRequested_ || !queue_.empty();
                });

                if (stopRequested_ && queue_.empty()) {
                    break;
                }

                task = std::move(queue_.front());
                queue_.pop_front();
            }

            try {
                task();
            } catch (...) {
                taskFailures_.fetch_add(1u, std::memory_order_relaxed);
            }

            tasksCompleted_.fetch_add(1u, std::memory_order_relaxed);
            tasksPending_.fetch_sub(1u, std::memory_order_relaxed);
        }
    }

    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<std::function<void()>> queue_{};
    std::thread worker_{};
    bool running_ = false;
    bool stopRequested_ = false;

    std::atomic<std::size_t> tasksSubmitted_{0};
    std::atomic<std::size_t> tasksCompleted_{0};
    std::atomic<std::size_t> tasksPending_{0};
    std::atomic<std::size_t> taskFailures_{0};
};

} // namespace BMMQ

#endif // BMMQ_BACKGROUND_TASK_SERVICE_HPP
