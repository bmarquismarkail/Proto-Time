#include "MachineLifecycleCoordinator.hpp"

#include "AudioService.hpp"
#include "VideoService.hpp"

namespace BMMQ {

namespace {
class LifecycleMutationScope {
public:
    LifecycleMutationScope(AudioService* audio, VideoService* video)
        : audio_(audio),
          video_(video)
    {
        if (audio_ != nullptr) {
            audio_->beginLifecycleMutationScope();
        }
        if (video_ != nullptr) {
            video_->beginLifecycleMutationScope();
        }
    }

    ~LifecycleMutationScope()
    {
        if (video_ != nullptr) {
            video_->endLifecycleMutationScope();
        }
        if (audio_ != nullptr) {
            audio_->endLifecycleMutationScope();
        }
    }

private:
    AudioService* audio_ = nullptr;
    VideoService* video_ = nullptr;
};
} // namespace

bool MachineLifecycleCoordinator::pauseLanesLocked(bool& wasVideoActive) noexcept
{
    bool ok = true;
    wasVideoActive = false;
    if (audioService_ != nullptr) {
        audioService_->setBackendPausedOrClosed(true);
    }
    if (videoService_ != nullptr) {
        wasVideoActive = videoService_->state() == VideoLifecycleState::Active;
        ok = videoService_->pause() && ok;
    }
    return ok;
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

bool MachineLifecycleCoordinator::bumpEpochsLocked() noexcept
{
    if (audioService_ != nullptr) {
        audioService_->bumpLifecycleEpochBarrier();
    }
    if (videoService_ != nullptr) {
        videoService_->bumpLifecycleEpochBarrier();
    }
    return true;
}

bool MachineLifecycleCoordinator::runTransition(MachineTransitionReason reason, const TransitionMutation& mutation)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto start = std::chrono::steady_clock::now();
    ++stats_.transitionCount;
    ++stats_.reasonCounts[static_cast<std::size_t>(reason)];

    MachineTransitionResult result;
    result.reason = reason;
    if (transitionDepth_ != 0u) {
        ++stats_.transitionReentryAttemptCount;
        ++stats_.nestedTransitionRejectCount;
        ++stats_.failureCount;
        result.outcome = MachineTransitionOutcome::Failed;
        result.failureStage = MachineTransitionFailureStage::Mutation;
        result.rejectedForReentry = true;
        lastResult_ = result;
        noteDurationLocked(std::chrono::steady_clock::now() - start);
        return false;
    }

    ++transitionDepth_;
    activeTransitionReason_ = reason;
    LifecycleMutationScope mutationScope(audioService_, videoService_);
    const auto policy = policies_[static_cast<std::size_t>(reason)];

    MachineTransitionMutationResult mutationResult{};
    bool pauseOk = false;
    bool bumpOk = false;
    bool resumeOk = false;
    bool wasVideoActive = false;
    std::size_t attempts = 0;
    for (; attempts <= policy.maxRetries; ++attempts) {
        pauseOk = pauseLanesLocked(wasVideoActive);
        if (!pauseOk) {
            result.failureStage = MachineTransitionFailureStage::Pause;
            break;
        }
        mutationResult = mutation ? mutation() : MachineTransitionMutationResult{};
        if (mutationResult.success) {
            break;
        }
    }
    result.retryCountUsed = attempts > 0u ? (attempts - 1u) : 0u;

    if (!pauseOk) {
        result.outcome = MachineTransitionOutcome::Failed;
        ++stats_.failureCount;
        lastResult_ = result;
        noteDurationLocked(std::chrono::steady_clock::now() - start);
        --transitionDepth_;
        return false;
    }

    bumpOk = bumpEpochsLocked();
    if (!bumpOk) {
        result.outcome = MachineTransitionOutcome::Failed;
        result.failureStage = MachineTransitionFailureStage::EpochBump;
        ++stats_.failureCount;
        lastResult_ = result;
        noteDurationLocked(std::chrono::steady_clock::now() - start);
        --transitionDepth_;
        return false;
    }

    resumeOk = resumeLanesLocked(wasVideoActive);
    if (!resumeOk) {
        result.outcome = MachineTransitionOutcome::Failed;
        result.failureStage = MachineTransitionFailureStage::Resume;
        ++stats_.failureCount;
        lastResult_ = result;
        noteDurationLocked(std::chrono::steady_clock::now() - start);
        --transitionDepth_;
        return false;
    }

    if (mutationResult.success) {
        result.outcome = MachineTransitionOutcome::Succeeded;
        result.failureStage = MachineTransitionFailureStage::None;
        ++stats_.successCount;
    } else {
        result.failureStage = MachineTransitionFailureStage::Mutation;
        const bool canDegrade = !policy.failHard &&
                                ((policy.allowHeadlessVideoFallback && !mutationResult.videoReady) ||
                                 (policy.allowAudioBackendDisable && !mutationResult.audioReady));
        if (canDegrade) {
            result.outcome = MachineTransitionOutcome::Degraded;
            result.degradedHeadlessVideo = policy.allowHeadlessVideoFallback && !mutationResult.videoReady;
            result.degradedAudioDisabled = policy.allowAudioBackendDisable && !mutationResult.audioReady;
            degradedHeadlessVideoActive_ = degradedHeadlessVideoActive_ || result.degradedHeadlessVideo;
            degradedAudioDisabledActive_ = degradedAudioDisabledActive_ || result.degradedAudioDisabled;
            ++stats_.degradedCount;
        } else {
            result.outcome = MachineTransitionOutcome::Failed;
            ++stats_.failureCount;
            lastResult_ = result;
            noteDurationLocked(std::chrono::steady_clock::now() - start);
            --transitionDepth_;
            return false;
        }
    }

    lastResult_ = result;
    noteDurationLocked(std::chrono::steady_clock::now() - start);
    --transitionDepth_;
    return true;
}

} // namespace BMMQ
