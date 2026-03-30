#ifndef INST_CYCLE_EXECUTOR_HPP
#define INST_CYCLE_EXECUTOR_HPP

#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

        auto execBlock = context.decode(fb);
        context.execute(execBlock, fb);
        result.executed = true;
        result.feedback = context.getLastFeedback();

        if (!result.usedScript && recordingEnabled_) {
            recordBlock(fb, result.feedback);
        }

        return result;
    }

    bool saveScript(const std::string& path, std::string* error = nullptr) const {
        std::ofstream out(path);
        if (!out.is_open()) {
            if (error != nullptr) *error = "failed to open output file";
            return false;
        }

        out << "TIME_EXECUTOR_BLOCKS_V1\n";
        const auto scriptSegments = normalizedSegments();
        for (const auto& seg : scriptSegments) {
            out << "SEG id=" << std::hex << std::uppercase << seg.id << "\n";
            for (const auto& block : seg.blocks) {
                out << "BLOCK base=" << std::hex << std::uppercase << block.getbaseAddress() << " data=";
                const auto& entries = block.getblockData();
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    if (i != 0) out << "|";
                    out << std::hex << std::uppercase << entries[i].offset << ":";
                    for (std::size_t j = 0; j < entries[i].data.size(); ++j) {
                        if (j != 0) out << ".";
                        const auto byte = entries[i].data[j];
                        if (byte < 0x10) out << '0';
                        out << static_cast<int>(byte);
                    }
                }
                out << "\n";
            }
            out << "ENDSEG\n";
        }

        return true;
    }

    bool loadScript(const std::string& path, std::string* error = nullptr) {
        std::ifstream in(path);
        if (!in.is_open()) {
            if (error != nullptr) *error = "failed to open script file";
            return false;
        }

        std::string header;
        if (!std::getline(in, header) || header != "TIME_EXECUTOR_BLOCKS_V1") {
            if (error != nullptr) *error = "invalid script header";
            return false;
        }

        segments_.clear();
        playbackBlocks_.clear();
        playbackIndex_ = 0;

        Segment* currentSegment = nullptr;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;

            if (line.rfind("SEG ", 0) == 0) {
                Segment seg{};
                seg.id = segments_.size();
                segments_.push_back(seg);
                currentSegment = &segments_.back();
                continue;
            }

            if (line == "ENDSEG") {
                currentSegment = nullptr;
                continue;
            }

            if (line.rfind("BLOCK ", 0) == 0) {
                if (currentSegment == nullptr) {
                    if (error != nullptr) *error = "BLOCK encountered outside segment";
                    return false;
                }

                auto parsed = parseBlock(line.substr(6));
                if (!parsed.first) {
                    if (error != nullptr) *error = parsed.second;
                    return false;
                }

                currentSegment->blocks.push_back(parsed.third);
                playbackBlocks_.push_back(parsed.third);
            }
        }

        return true;
    }

private:
    struct ParseBlockResult {
        bool first = false;
        std::string second;
        FetchBlock third;
    };

    static ParseBlockResult parseBlock(const std::string& payload) {
        ParseBlockResult result{};
        FetchBlock block;

        std::string baseToken;
        std::string dataToken;
        std::stringstream ss(payload);
        ss >> baseToken >> dataToken;

        if (baseToken.rfind("base=", 0) != 0 || dataToken.rfind("data=", 0) != 0) {
            result.second = "invalid block tokens";
            return result;
        }

        const auto parseHex = [](const std::string& value) -> uint64_t {
            return std::stoull(value, nullptr, 16);
        };

        block.setbaseAddress(static_cast<AddressType>(parseHex(baseToken.substr(5))));
        const auto encodedEntries = dataToken.substr(5);

        std::stringstream entryStream(encodedEntries);
        std::string encodedEntry;
        while (std::getline(entryStream, encodedEntry, '|')) {
            if (encodedEntry.empty()) continue;
            const auto colon = encodedEntry.find(':');
            if (colon == std::string::npos) {
                result.second = "invalid block entry";
                return result;
            }

            fetchBlockData<AddressType, DataType> entry{};
            entry.offset = static_cast<AddressType>(parseHex(encodedEntry.substr(0, colon)));

            std::stringstream byteStream(encodedEntry.substr(colon + 1));
            std::string byteToken;
            while (std::getline(byteStream, byteToken, '.')) {
                if (byteToken.empty()) continue;
                entry.data.push_back(static_cast<DataType>(parseHex(byteToken)));
            }

            block.getblockData().push_back(std::move(entry));
        }

        result.first = true;
        result.third = std::move(block);
        return result;
    }

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
