#ifndef INST_CYCLE_EXECUTOR_HPP
#define INST_CYCLE_EXECUTOR_HPP

#include <functional>
#include <string>
#include <vector>

#include "BlockScript.hpp"
#include "../../machine/RuntimeContext.hpp"
#include "../fetch/fetchBlock.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
class Executor {
public:
    using FetchBlock = fetchBlock<AddressType, DataType>;
    using SegmentPredicate = std::function<bool(const FetchBlock&, const CpuFeedback&)>;

    struct Segment {
        std::size_t id = 0;
        std::vector<FetchBlock> blocks;
    };

    struct StepResult {
        bool usedScript = false;
        bool executed = false;
        ExecutionGuarantee guarantee = ExecutionGuarantee::Experimental;
        CpuFeedback feedback {};
    };

    explicit Executor(SegmentPredicate splitPredicate = {})
        : splitPredicate_(std::move(splitPredicate)) {}

    void enableRecording(bool enabled = true) { recordingEnabled_ = enabled; }
    void setSegmentPredicate(SegmentPredicate predicate) {
        splitPredicate_ = std::move(predicate);
    }

    const std::vector<FetchBlock>& recordedBlocks() const { return recordedBlocks_; }
    const std::vector<Segment>& recordedSegments() const { return segments_; }

    StepResult step(RuntimeContext& context) {
        FetchBlock fb;
        StepResult result{};

        if (playbackIndex_ < playbackBlocks_.size()) {
            fb = playbackBlocks_[playbackIndex_++];
            result.usedScript = true;
        } else {
            fb = context.fetch();
        }

        result.feedback = context.step(fb);
        result.executed = true;
        result.guarantee = context.guarantee();

        if (!result.usedScript && recordingEnabled_) {
            recordBlock(fb, result.feedback);
        }

        return result;
    }

    bool saveScript(const std::string& path, std::string* error = nullptr) const {
        return ExecutorIO::saveBlockScript<FetchBlock, Segment>(
            path,
            normalizedSegments(),
            error);
    }

    bool loadScript(const std::string& path, std::string* error = nullptr) {
        segments_.clear();
        recordedBlocks_.clear();
        playbackBlocks_.clear();
        playbackIndex_ = 0;
        return ExecutorIO::loadBlockScript<FetchBlock>(
            path,
            &segments_,
            &playbackBlocks_,
            [](std::size_t id) { return Segment{id, {}}; },
            error);
    }

private:
    std::vector<Segment> normalizedSegments() const {
        std::vector<Segment> nonEmpty;
        for (const auto& seg : segments_) {
            if (!seg.blocks.empty()) nonEmpty.push_back(seg);
        }
        if (!nonEmpty.empty()) return nonEmpty;

        Segment fallback{};
        fallback.id = 0;
        fallback.blocks = recordedBlocks_;
        if (!fallback.blocks.empty()) nonEmpty.push_back(std::move(fallback));
        return nonEmpty;
    }

    void recordBlock(const FetchBlock& block, const CpuFeedback& feedback) {
        recordedBlocks_.push_back(block);

        if (segments_.empty()) {
            segments_.push_back(Segment{0, {}});
        }

        segments_.back().blocks.push_back(block);

        if (splitPredicate_ && splitPredicate_(block, feedback)) {
            segments_.push_back(Segment{segments_.size(), {}});
        }
    }

    bool recordingEnabled_ = true;
    SegmentPredicate splitPredicate_;

    std::vector<FetchBlock> recordedBlocks_;
    std::vector<Segment> segments_;

    std::vector<FetchBlock> playbackBlocks_;
    std::size_t playbackIndex_ = 0;
};

} // namespace BMMQ

#endif // INST_CYCLE_EXECUTOR_HPP
