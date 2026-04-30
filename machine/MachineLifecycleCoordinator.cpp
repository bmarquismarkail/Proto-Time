#include "MachineLifecycleCoordinator.hpp"

#include "AudioService.hpp"
#include "VideoService.hpp"

namespace BMMQ {

bool MachineLifecycleCoordinator::pauseLanesLocked() noexcept
{
    bool wasVideoActive = false;
    if (audioService_ != nullptr) {
        audioService_->setBackendPausedOrClosed(true);
    }
    if (videoService_ != nullptr) {
        wasVideoActive = videoService_->state() == VideoLifecycleState::Active;
        (void)videoService_->pause();
    }
    return wasVideoActive;
}

bool MachineLifecycleCoordinator::resumeLanesLocked(bool resumeVideo) noexcept
{
    bool ok = true;
    if (resumeVideo && videoService_ != nullptr) {
        ok = videoService_->resume() && ok;
    }
    if (audioService_ != nullptr) {
        audioService_->setBackendPausedOrClosed(false);
    }
    return ok;
}

void MachineLifecycleCoordinator::bumpEpochsLocked() noexcept
{
    if (audioService_ != nullptr) {
        audioService_->bumpLifecycleEpochBarrier();
    }
    if (videoService_ != nullptr) {
        videoService_->bumpLifecycleEpochBarrier();
    }
}

} // namespace BMMQ
