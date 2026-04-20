#ifndef BMMQ_TESTS_VISUAL_TEST_HELPERS_HPP
#define BMMQ_TESTS_VISUAL_TEST_HELPERS_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VisualTypes.hpp"
#include "machine/plugins/IoPlugin.hpp"

namespace BMMQ::Tests::Visual {

inline void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << contents;
}

inline void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
}

[[nodiscard]] inline std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] inline std::vector<uint8_t> makePng2x2Rgba()
{
    return {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
        0x24, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x0C, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49,
        0xC8, 0x09, 0xF7, 0xF9, 0xAB, 0xB6, 0x0D, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82,
    };
}

[[nodiscard]] inline std::vector<uint8_t> makeHeaderOnlyPng(uint32_t width, uint32_t height)
{
    return {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        static_cast<uint8_t>((width >> 24u) & 0xFFu),
        static_cast<uint8_t>((width >> 16u) & 0xFFu),
        static_cast<uint8_t>((width >> 8u) & 0xFFu),
        static_cast<uint8_t>(width & 0xFFu),
        static_cast<uint8_t>((height >> 24u) & 0xFFu),
        static_cast<uint8_t>((height >> 16u) & 0xFFu),
        static_cast<uint8_t>((height >> 8u) & 0xFFu),
        static_cast<uint8_t>(height & 0xFFu),
        0x08, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0x00, 0x00, 0x00, 0x00,
    };
}

[[nodiscard]] inline VideoStateView makeTileState(uint8_t lowByte, uint8_t highByte)
{
    VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x91u;
    state.bgp = 0xE4u;
    for (std::size_t row = 0; row < 8u; ++row) {
        state.vram[row * 2u] = lowByte;
        state.vram[row * 2u + 1u] = highByte;
    }
    state.vram[0x1800u] = 0x00u;
    return state;
}

inline void writeSingleRulePack(const std::filesystem::path& manifestPath,
                                std::string_view id,
                                VisualResourceKind kind,
                                VisualResourceHash hash,
                                const std::filesystem::path& imagePath)
{
    writeBinaryFile(imagePath, makePng2x2Rgba());
    writeTextFile(manifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"" + std::string(id) + "\",\n"
        "  \"name\": \"Smoke Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"" + visualResourceKindName(kind) + "\",\n"
        "        \"decodedHash\": \"" + toHexVisualHash(hash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"" + imagePath.filename().string() + "\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
}

} // namespace BMMQ::Tests::Visual

#endif // BMMQ_TESTS_VISUAL_TEST_HELPERS_HPP
