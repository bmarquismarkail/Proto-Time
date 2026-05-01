#include "DebugSnapshotService.hpp"

#include <algorithm>

namespace BMMQ {

DebugSnapshotService::DebugSnapshotService(
    std::size_t videoCapacity, std::size_t audioCapacity) noexcept
    : videoCapacity_(std::max<std::size_t>(videoCapacity, 1u))
    , audioCapacity_(std::max<std::size_t>(audioCapacity, 1u))
{
}

bool DebugSnapshotService::submitVideoModel(std::optional<VideoDebugFrameModel> model)
{
    if (!model.has_value()) {
        return true;  // no-op for nullopt
    }
    videoSubmissions_.fetch_add(1u, std::memory_order_relaxed);
    std::scoped_lock<std::mutex> lock(videoMutex_);
    if (videoQueue_.size() >= videoCapacity_) {
        videoOverflows_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    videoQueue_.push_back(std::move(*model));
    return true;
}

bool DebugSnapshotService::submitAudioState(std::optional<AudioStateView> state)
{
    if (!state.has_value()) {
        return true;  // no-op for nullopt
    }
    audioSubmissions_.fetch_add(1u, std::memory_order_relaxed);
    std::scoped_lock<std::mutex> lock(audioMutex_);
    if (audioQueue_.size() >= audioCapacity_) {
        audioOverflows_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    audioQueue_.push_back(std::move(*state));
    return true;
}

std::optional<VideoDebugFrameModel> DebugSnapshotService::tryConsumeVideo()
{
    std::scoped_lock<std::mutex> lock(videoMutex_);
    if (videoQueue_.empty()) {
        return std::nullopt;
    }
    videoConsumptions_.fetch_add(1u, std::memory_order_relaxed);
    auto result = std::move(videoQueue_.front());
    videoQueue_.pop_front();
    return result;
}

std::optional<AudioStateView> DebugSnapshotService::tryConsumeAudio()
{
    std::scoped_lock<std::mutex> lock(audioMutex_);
    if (audioQueue_.empty()) {
        return std::nullopt;
    }
    audioConsumptions_.fetch_add(1u, std::memory_order_relaxed);
    auto result = std::move(audioQueue_.front());
    audioQueue_.pop_front();
    return result;
}

DebugSnapshotStats DebugSnapshotService::stats() const noexcept
{
    return DebugSnapshotStats{
        .videoSubmissions = videoSubmissions_.load(std::memory_order_relaxed),
        .videoConsumptions = videoConsumptions_.load(std::memory_order_relaxed),
        .videoOverflows = videoOverflows_.load(std::memory_order_relaxed),
        .audioSubmissions = audioSubmissions_.load(std::memory_order_relaxed),
        .audioConsumptions = audioConsumptions_.load(std::memory_order_relaxed),
        .audioOverflows = audioOverflows_.load(std::memory_order_relaxed),
    };
}

} // namespace BMMQ
