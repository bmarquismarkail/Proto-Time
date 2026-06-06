#include "cores/gamegear/GameGearMachine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct BaselineCase {
    std::string label;
    std::filesystem::path romPath;
    std::uint64_t steps = 1000000u;
    std::size_t minUniqueColors = 0u;
    std::uint64_t minAudioFrames = 0u;
    bool requireDisplayEnabled = false;
    bool requirePcMoved = true;
};

[[nodiscard]] std::string trim(std::string_view text)
{
    auto begin = text.begin();
    auto end = text.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }
    return std::string(begin, end);
}

[[nodiscard]] std::vector<std::string> splitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0u;
    while (start <= line.size()) {
        const auto tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1u;
    }
    return fields;
}

[[nodiscard]] std::uint64_t parseUnsigned(const std::string& value,
                                          std::uint64_t fallback,
                                          const std::string& label)
{
    const auto text = trim(value);
    if (text.empty()) {
        return fallback;
    }
    try {
        std::size_t parsed = 0u;
        const auto result = std::stoull(text, &parsed, 0);
        if (parsed != text.size()) {
            throw std::invalid_argument("trailing text");
        }
        return static_cast<std::uint64_t>(result);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid unsigned value for " + label + ": " + text);
    }
}

[[nodiscard]] bool parseBool(const std::string& value, bool fallback, const std::string& label)
{
    auto text = trim(value);
    if (text.empty()) {
        return fallback;
    }
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        return false;
    }
    throw std::runtime_error("invalid boolean value for " + label + ": " + text);
}

[[nodiscard]] std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open ROM: " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        throw std::runtime_error("empty ROM: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::vector<BaselineCase> loadManifest(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open baseline manifest: " + path.string());
    }

    std::vector<BaselineCase> cases;
    const auto baseDir = path.parent_path();
    std::string line;
    std::uint64_t lineNumber = 0u;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }

        const auto fields = splitTabs(text);
        if (fields.size() < 2u) {
            throw std::runtime_error("baseline manifest line " + std::to_string(lineNumber) +
                                     " must contain at least label and ROM path");
        }

        BaselineCase testCase;
        testCase.label = trim(fields[0]);
        testCase.romPath = std::filesystem::path(trim(fields[1]));
        if (testCase.romPath.is_relative()) {
            testCase.romPath = baseDir / testCase.romPath;
        }
        if (fields.size() > 2u) {
            testCase.steps = parseUnsigned(fields[2], testCase.steps, "steps");
        }
        if (fields.size() > 3u) {
            testCase.minUniqueColors = static_cast<std::size_t>(
                parseUnsigned(fields[3], testCase.minUniqueColors, "min_unique_colors"));
        }
        if (fields.size() > 4u) {
            testCase.minAudioFrames = parseUnsigned(fields[4], testCase.minAudioFrames, "min_audio_frames");
        }
        if (fields.size() > 5u) {
            testCase.requireDisplayEnabled = parseBool(fields[5], testCase.requireDisplayEnabled, "require_display_enabled");
        }
        if (fields.size() > 6u) {
            testCase.requirePcMoved = parseBool(fields[6], testCase.requirePcMoved, "require_pc_moved");
        }
        if (testCase.label.empty()) {
            throw std::runtime_error("baseline manifest line " + std::to_string(lineNumber) + " has empty label");
        }
        cases.push_back(std::move(testCase));
    }
    return cases;
}

[[nodiscard]] std::size_t uniqueFrameColors(const BMMQ::VideoDebugFrameModel& frame)
{
    std::unordered_set<std::uint32_t> colors;
    colors.reserve(frame.argbPixels.size());
    for (const auto color : frame.argbPixels) {
        colors.insert(color);
    }
    return colors.size();
}

bool runCase(const BaselineCase& testCase)
{
    BMMQ::GameGearMachine machine;
    machine.loadRom(readBinaryFile(testCase.romPath));

    const auto initialPc = machine.readRegisterPair("PC");
    for (std::uint64_t step = 0u; step < testCase.steps; ++step) {
        machine.step();
        if ((step % 1024u) == 0u) {
            machine.serviceInput();
        }
    }

    const auto pc = machine.readRegisterPair("PC");
    const auto frame = machine.videoDebugFrameModel({160, 144});
    const auto colorCount = frame.has_value() && !frame->empty() ? uniqueFrameColors(*frame) : 0u;
    const auto audioFrames = machine.audioFrameCounter();

    std::cout << "rom-baseline label=" << testCase.label
              << " steps=" << testCase.steps
              << " pc=0x" << std::hex << pc << std::dec
              << " colors=" << colorCount
              << " display=" << (frame.has_value() && frame->displayEnabled ? "on" : "off")
              << " audio_frames=" << audioFrames << '\n';

    if (testCase.requirePcMoved && pc == initialPc) {
        std::cerr << testCase.label << ": PC did not move from reset value\n";
        return false;
    }
    if (testCase.requireDisplayEnabled && (!frame.has_value() || !frame->displayEnabled)) {
        std::cerr << testCase.label << ": display was not enabled\n";
        return false;
    }
    if (colorCount < testCase.minUniqueColors) {
        std::cerr << testCase.label << ": unique frame color count " << colorCount
                  << " below required " << testCase.minUniqueColors << '\n';
        return false;
    }
    if (audioFrames < testCase.minAudioFrames) {
        std::cerr << testCase.label << ": audio frame count " << audioFrames
                  << " below required " << testCase.minAudioFrames << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const char* manifestPath = std::getenv("PROTO_TIME_GG_ROM_BASELINE");
    if (manifestPath == nullptr || trim(manifestPath).empty()) {
        std::cout << "PROTO_TIME_GG_ROM_BASELINE is not set; skipping optional Game Gear ROM baseline corpus\n";
        return 0;
    }

    try {
        const auto cases = loadManifest(manifestPath);
        if (cases.empty()) {
            std::cout << "Game Gear ROM baseline manifest contains no cases; skipping\n";
            return 0;
        }

        bool ok = true;
        for (const auto& testCase : cases) {
            ok = runCase(testCase) && ok;
        }
        return ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "smoke_gamegear_rom_baseline failed: " << ex.what() << '\n';
        return 1;
    }
}
