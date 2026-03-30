#ifndef PLUGIN_EXECUTOR_HPP
#define PLUGIN_EXECUTOR_HPP

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
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
        bool usedScript = false;
        bool executed = false;
        BMMQ::ExecutionGuarantee guarantee = BMMQ::ExecutionGuarantee::Experimental;
        BMMQ::CpuFeedback feedback {};
    };

    explicit PluginExecutor(IExecutorPolicyPlugin& policy)
        : policy_(policy) {}

    bool saveScript(const std::string& path, std::string* error = nullptr) const {
        std::ofstream out(path);
        if (!out.is_open()) {
            if (error != nullptr) *error = "failed to open output file";
            return false;
        }

        out << "TIME_EXECUTOR_BLOCKS_V1\n";
        for (const auto& seg : segments_) {
            if (seg.blocks.empty()) continue;
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
        recordedBlocks_.clear();
        playbackBlocks_.clear();
        playbackIndex_ = 0;

        Segment* currentSegment = nullptr;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;

            if (line.rfind("SEG ", 0) == 0) {
                segments_.push_back(Segment{segments_.size(), {}});
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

            BMMQ::fetchBlockData<AddressType, DataType> entry{};
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
