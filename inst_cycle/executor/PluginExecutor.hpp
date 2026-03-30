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

    explicit PluginExecutor(IExecutorPolicyPlugin& policy)
        : policy_(policy) {}

    bool saveScript(const std::string& path, std::string* error = nullptr) const {
        return BMMQ::ExecutorIO::saveBlockScript<FetchBlock, Segment>(
            path,
            segments_,
            error);
    }

    bool loadScript(const std::string& path, std::string* error = nullptr) {
        segments_.clear();
        recordedBlocks_.clear();
        playbackBlocks_.clear();
        playbackIndex_ = 0;
        return BMMQ::ExecutorIO::loadBlockScript<FetchBlock>(
            path,
            &segments_,
            &playbackBlocks_,
            [](std::size_t id) { return Segment{id, {}}; },
            error);
    }

    StepResult step(BMMQ::RuntimeContext& context) {
        FetchBlock fetchBlock;
        StepResult result{};

        if (playbackIndex_ < playbackBlocks_.size()) {
            fetchBlock = playbackBlocks_[playbackIndex_++];
            result.usedScript = true;
        } else {
            fetchBlock = context.fetch();
        }

        const auto feedback = context.step(fetchBlock);
        recordIfNeeded(fetchBlock, feedback, result.usedScript);
        result.executed = true;
        result.guarantee = policy_.guarantee();
        result.feedback = feedback;
        return result;
    }

    const std::vector<FetchBlock>& recordedBlocks() const { return recordedBlocks_; }
    const std::vector<Segment>& segments() const { return segments_; }

private:
    void recordIfNeeded(const FetchBlock& block, const BMMQ::CpuFeedback& feedback, bool usedScript) {
        if (usedScript || !policy_.shouldRecord(block, feedback)) return;

        if (segments_.empty()) {
            segments_.push_back(Segment{0, {}});
        }

        recordedBlocks_.push_back(block);
        segments_.back().blocks.push_back(block);

        if (policy_.shouldSegment(block, feedback)) {
            segments_.push_back(Segment{segments_.size(), {}});
        }
    }

    IExecutorPolicyPlugin& policy_;
    std::vector<FetchBlock> recordedBlocks_;
    std::vector<Segment> segments_;
    std::vector<FetchBlock> playbackBlocks_;
    std::size_t playbackIndex_ = 0;
};

} // namespace BMMQ::Plugin

#endif // PLUGIN_EXECUTOR_HPP
