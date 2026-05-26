#include "VideoService.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "machine/VideoService.hpp"
#include "machine/Machine.hpp"
#include "machine/threading/UiRenderThread.hpp"

namespace BMMQ {

VideoService::VideoService(
    std::shared_ptr<Machine> machine,
    std::unique_ptr<AudioService> audioService,
    std::unique_ptr<InputService> inputService,
    std::unique_ptr<VisualOverrideService> visualOverrideService,
    std::unique_ptr<VisualDebugAdapter> visualDebugAdapter)
    : Machine(std::move(machine), std::move(audioService),
              std::move(inputService), std::move(visualOverrideService),
              std::move(visualDebugAdapter))
{
    connectVideoServiceToVisualOverride();
    bindVisualOverrideEvents();
    lifecycleCoordinator_.bindServices(audioService.get(), this);
}

[[nodiscard]] bool VideoService::start()
{
    setState(VideoLifecycleState::Active);
    return true;
}

void VideoService::stop()
{
    setState(VideoLifecycleState::Detached);
}

[[nodiscard]] bool VideoService::isRunning() const noexcept
{
    return state_ == VideoLifecycleState::Active || state_ == VideoLifecycleState::Headless;
}

[[nodiscard]] bool VideoService::isHeadless() const noexcept
{
    return state_ == VideoLifecycleState::Headless || presenter_ == nullptr;
}

[[nodiscard]] bool VideoService::hasActivePresenter() const noexcept
{
    return presenter_ != nullptr && presenter_->ready();
}

void VideoService::signalFramePresent(std::shared_ptr<VideoFramePacket> packet) noexcept
{
    if (uiRenderThread_)
    {
        uiRenderThread_->signalFrameReady(std::move(packet));
    }
}

} // namespace BMMQ
