#include "DebugSnapshotService.hpp"

#include <algorithm>

#include "machine/BackgroundTaskService.hpp"

namespace BMMQ {

struct DebugSnapshotService::State {
    explicit State(std::size_t videoCapacityValue, std::size_t audioCapacityValue) noexcept
        : videoCapacity(std::max<std::size_t>(videoCapacityValue, 1u))
        , audioCapacity(std::max<std::size_t>(audioCapacityValue, 1u))
    {
    }

    const std::size_t videoCapacity;
    const std::size_t audioCapacity;

    mutable std::mutex videoMutex{};
    std::deque<VideoDebugFrameModel> videoQueue{};

    mutable std::mutex audioMutex{};
    std::deque<AudioStateView> audioQueue{};

    std::atomic<std::size_t> videoSubmissions{0};
    std::atomic<std::size_t> videoConsumptions{0};
    std::atomic<std::size_t> videoOverflows{0};
    std::atomic<std::size_t> audioSubmissions{0};
    std::atomic<std::size_t> audioConsumptions{0};
    std::atomic<std::size_t> audioOverflows{0};
    std::atomic<std::size_t> videoBackgroundSubmissions{0};
    std::atomic<std::size_t> videoBackgroundFallbacks{0};
    std::atomic<std::size_t> audioBackgroundSubmissions{0};
    std::atomic<std::size_t> audioBackgroundFallbacks{0};
};

DebugSnapshotService::DebugSnapshotService(
    std::size_t videoCapacity, std::size_t audioCapacity) noexcept
    : state_(std::make_shared<State>(videoCapacity, audioCapacity))
{
}

void DebugSnapshotService::setBackgroundTaskService(BackgroundTaskService* service) noexcept
{
    backgroundTaskService_ = service;
}

bool DebugSnapshotService::submitVideoModel(std::optional<VideoDebugFrameModel> model)
{
    if (!model.has_value()) {
        return true;
    }

    auto state = state_;
    if (backgroundTaskService_ != nullptr) {
        auto taskModel = std::move(*model);
        const bool queued = backgroundTaskService_->submit(BackgroundJobCategory::DebugSnapshot,
            [state, taskModel = std::move(taskModel)]() mutable {
            (void)DebugSnapshotService::enqueueVideo(state, std::move(taskModel));
        });
        if (queued) {
            state->videoBackgroundSubmissions.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }
        state->videoBackgroundFallbacks.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }

    return enqueueVideo(state, std::move(*model));
}

bool DebugSnapshotService::submitAudioState(std::optional<AudioStateView> stateView)
{
    if (!stateView.has_value()) {
        return true;
    }

    auto state = state_;
    if (backgroundTaskService_ != nullptr) {
        auto taskState = std::move(*stateView);
        const bool queued = backgroundTaskService_->submit(BackgroundJobCategory::DebugSnapshot,
            [state, taskState = std::move(taskState)]() mutable {
            (void)DebugSnapshotService::enqueueAudio(state, std::move(taskState));
        });
        if (queued) {
            state->audioBackgroundSubmissions.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }
        state->audioBackgroundFallbacks.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }

    return enqueueAudio(state, std::move(*stateView));
}

bool DebugSnapshotService::submitVideoState(
    VideoStateView stateView,
    const IVisualDebugAdapter* adapter,
    VideoDebugRenderRequest request)
{
    if (backgroundTaskService_ == nullptr || adapter == nullptr) {
        return false;
    }
    auto state = state_;
    const bool queued = backgroundTaskService_->submit(BackgroundJobCategory::DebugSnapshot,
        [state, stateView = std::move(stateView), adapter, request]() mutable {
            auto model = adapter->buildFrameModelFromState(stateView, request);
            if (model.has_value()) {
                (void)DebugSnapshotService::enqueueVideo(state, std::move(*model));
            }
        });
    if (queued) {
        state->videoBackgroundSubmissions.fetch_add(1u, std::memory_order_relaxed);
    } else {
        state->videoBackgroundFallbacks.fetch_add(1u, std::memory_order_relaxed);
    }
    return queued;
}

std::optional<VideoDebugFrameModel> DebugSnapshotService::tryConsumeVideo()
{
    auto state = state_;
    std::scoped_lock<std::mutex> lock(state->videoMutex);
    if (state->videoQueue.empty()) {
        return std::nullopt;
    }
    state->videoConsumptions.fetch_add(1u, std::memory_order_relaxed);
    auto result = std::move(state->videoQueue.front());
    state->videoQueue.pop_front();
    return result;
}

std::optional<AudioStateView> DebugSnapshotService::tryConsumeAudio()
{
    auto state = state_;
    std::scoped_lock<std::mutex> lock(state->audioMutex);
    if (state->audioQueue.empty()) {
        return std::nullopt;
    }
    state->audioConsumptions.fetch_add(1u, std::memory_order_relaxed);
    auto result = std::move(state->audioQueue.front());
    state->audioQueue.pop_front();
    return result;
}

DebugSnapshotStats DebugSnapshotService::stats() const noexcept
{
    auto state = state_;
    return DebugSnapshotStats{
        .videoSubmissions = state->videoSubmissions.load(std::memory_order_relaxed),
        .videoConsumptions = state->videoConsumptions.load(std::memory_order_relaxed),
        .videoOverflows = state->videoOverflows.load(std::memory_order_relaxed),
        .audioSubmissions = state->audioSubmissions.load(std::memory_order_relaxed),
        .audioConsumptions = state->audioConsumptions.load(std::memory_order_relaxed),
        .audioOverflows = state->audioOverflows.load(std::memory_order_relaxed),
        .videoBackgroundSubmissions = state->videoBackgroundSubmissions.load(std::memory_order_relaxed),
        .videoBackgroundFallbacks = state->videoBackgroundFallbacks.load(std::memory_order_relaxed),
        .audioBackgroundSubmissions = state->audioBackgroundSubmissions.load(std::memory_order_relaxed),
        .audioBackgroundFallbacks = state->audioBackgroundFallbacks.load(std::memory_order_relaxed),
    };
}

bool DebugSnapshotService::enqueueVideo(const std::shared_ptr<State>& state, VideoDebugFrameModel model)
{
    state->videoSubmissions.fetch_add(1u, std::memory_order_relaxed);
    std::scoped_lock<std::mutex> lock(state->videoMutex);
    if (state->videoQueue.size() >= state->videoCapacity) {
        state->videoOverflows.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    state->videoQueue.push_back(std::move(model));
    return true;
}

bool DebugSnapshotService::enqueueAudio(const std::shared_ptr<State>& state, AudioStateView stateView)
{
    state->audioSubmissions.fetch_add(1u, std::memory_order_relaxed);
    std::scoped_lock<std::mutex> lock(state->audioMutex);
    if (state->audioQueue.size() >= state->audioCapacity) {
        state->audioOverflows.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    state->audioQueue.push_back(std::move(stateView));
    return true;
}

} // namespace BMMQ
