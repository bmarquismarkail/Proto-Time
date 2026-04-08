#ifndef BLOCK_SCRIPT_HPP
#define BLOCK_SCRIPT_HPP

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../fetch/fetchBlock.hpp"

namespace BMMQ::ExecutorIO {

template<typename FetchBlock>
struct ParseBlockResult {
    bool ok = false;
    std::string error;
    FetchBlock block;
};

template<typename FetchBlock, typename Segment>
void saveBlockScript(const std::string& path,
                     const std::vector<Segment>& segments) {
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("failed to open output file");
    }

    out << "TIME_EXECUTOR_BLOCKS_V1\n";
    for (const auto& seg : segments) {
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
}

template<typename FetchBlock>
ParseBlockResult<FetchBlock> parseBlockPayload(const std::string& payload) {
    using AddressType = std::decay_t<decltype(std::declval<FetchBlock>().getbaseAddress())>;
    using EntryType = std::decay_t<decltype(std::declval<FetchBlock>().getblockData()[0])>;
    using DataType = std::decay_t<decltype(std::declval<EntryType>().data[0])>;

    ParseBlockResult<FetchBlock> result{};
    FetchBlock block;

    std::string baseToken;
    std::string dataToken;
    std::stringstream ss(payload);
    ss >> baseToken >> dataToken;

    if (baseToken.rfind("base=", 0) != 0 || dataToken.rfind("data=", 0) != 0) {
        result.error = "invalid block tokens";
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
            result.error = "invalid block entry";
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

    result.ok = true;
    result.block = std::move(block);
    return result;
}

template<typename FetchBlock, typename SegmentFactory>
void loadBlockScript(const std::string& path,
                     std::vector<typename std::invoke_result_t<SegmentFactory, std::size_t>>* segments,
                     std::vector<FetchBlock>* playbackBlocks,
                     SegmentFactory makeSegment) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("failed to open script file");
    }

    std::string header;
    if (!std::getline(in, header) || header != "TIME_EXECUTOR_BLOCKS_V1") {
        throw std::runtime_error("invalid script header");
    }

    segments->clear();
    playbackBlocks->clear();

    auto* currentSegment = static_cast<typename std::invoke_result_t<SegmentFactory, std::size_t>*>(nullptr);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (line.rfind("SEG ", 0) == 0) {
            segments->push_back(makeSegment(segments->size()));
            currentSegment = &segments->back();
            continue;
        }

        if (line == "ENDSEG") {
            currentSegment = nullptr;
            continue;
        }

        if (line.rfind("BLOCK ", 0) == 0) {
            if (currentSegment == nullptr) {
                throw std::runtime_error("BLOCK encountered outside segment");
            }

            auto parsed = parseBlockPayload<FetchBlock>(line.substr(6));
            if (!parsed.ok) {
                throw std::runtime_error(parsed.error);
            }

            currentSegment->blocks.push_back(parsed.block);
            playbackBlocks->push_back(parsed.block);
        }
    }
}

} // namespace BMMQ::ExecutorIO

#endif // BLOCK_SCRIPT_HPP
