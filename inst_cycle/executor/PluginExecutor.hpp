#ifndef PLUGIN_EXECUTOR_HPP
#define PLUGIN_EXECUTOR_HPP

#include <cstddef>
#include <vector>

#include "PluginContract.hpp"

namespace BMMQ::Plugin {

class PluginExecutor {
public:
    struct Segment {
        std::size_t id = 0;
        std::vector<FetchBlock> blocks;
    };

    struct StepResult {
        bool executed = false;
        BMMQ::ExecutionGuarantee guarantee = BMMQ::ExecutionGuarantee::Experimental;
        BMMQ::CpuFeedback feedback {};
    };

    explicit PluginExecutor(IExecutorPolicyPlugin& policy)
        : policy_(policy) {}

    StepResult step(BMMQ::RuntimeContext& context) {
        auto fetchBlock = context.fetch();
        const auto feedback = context.step(fetchBlock);
        recordIfNeeded(fetchBlock, feedback);
        return StepResult{true, policy_.guarantee(), feedback};
    }

    const std::vector<FetchBlock>& recordedBlocks() const { return recordedBlocks_; }
    const std::vector<Segment>& segments() const { return segments_; }

private:
    void recordIfNeeded(const FetchBlock& block, const BMMQ::CpuFeedback& feedback) {
        if (!policy_.shouldRecord(block, feedback)) return;

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
};

} // namespace BMMQ::Plugin

#endif // PLUGIN_EXECUTOR_HPP
