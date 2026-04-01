#ifndef PLUGIN_EXECUTOR_HPP
#define PLUGIN_EXECUTOR_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "BlockScript.hpp"
#include "PluginContract.hpp"

namespace BMMQ::Plugin {

class PluginExecutor {
public:
    struct Segment {
        std::size_t id = 0;
        std::vector<FetchBlock> blocks;
    };

    struct StepResult {
        bool usedScript = false;
        bool executed = false;
        BMMQ::ExecutionGuarantee guarantee = BMMQ::ExecutionGuarantee::Experimental;
        BMMQ::CpuFeedback feedback {};
    };

    void saveScript(const std::string& path) const {
        BMMQ::ExecutorIO::saveBlockScript<FetchBlock, Segment>(
            path,
            segments_);
    }

    void loadScript(const std::string& path) {
        std::vector<Segment> tempSegments;
        std::vector<FetchBlock> tempPlaybackBlocks;
        std::size_t tempPlaybackIndex = 0;

        BMMQ::ExecutorIO::loadBlockScript<FetchBlock>(
            path,
            &tempSegments,
            &tempPlaybackBlocks,
            [](std::size_t id) { return Segment{id, {}}; });

        segments_ = std::move(tempSegments);
        playbackBlocks_ = std::move(tempPlaybackBlocks);
        recordedBlocks_.clear();
        playbackIndex_ = tempPlaybackIndex;
    }

    StepResult step(BMMQ::RuntimeContext& context) {
        FetchBlock fetchBlock;
        StepResult result{};
        const auto& policy = context.attachedExecutorPolicy();

        if (playbackIndex_ < playbackBlocks_.size()) {
            fetchBlock = playbackBlocks_[playbackIndex_++];
            result.usedScript = true;
        } else {
            fetchBlock = context.fetch();
        }

        const auto feedback = context.step(fetchBlock);
        recordIfNeeded(policy, fetchBlock, feedback, result.usedScript);
        result.executed = true;
        result.guarantee = context.guarantee();
        result.feedback = feedback;
        return result;
    }

    const std::vector<FetchBlock>& recordedBlocks() const { return recordedBlocks_; }
    const std::vector<Segment>& recordedSegments() const { return segments_; }
    const std::vector<Segment>& segments() const { return segments_; }

private:
    void recordIfNeeded(
        const IExecutorPolicyPlugin& policy,
        const FetchBlock& block,
        const BMMQ::CpuFeedback& feedback,
        bool usedScript) {
        if (usedScript || !policy.shouldRecord(block, feedback)) return;

        if (segments_.empty()) {
            segments_.push_back(Segment{0, {}});
        }

        recordedBlocks_.push_back(block);
        segments_.back().blocks.push_back(block);

        if (policy.shouldSegment(block, feedback)) {
            segments_.push_back(Segment{segments_.size(), {}});
        }
    }

    std::vector<FetchBlock> recordedBlocks_;
    std::vector<Segment> segments_;
    std::vector<FetchBlock> playbackBlocks_;
    std::size_t playbackIndex_ = 0;
};

} // namespace BMMQ::Plugin

#endif // PLUGIN_EXECUTOR_HPP
