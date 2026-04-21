#ifndef BMMQ_TESTS_VISUAL_TEST_HELPERS_HPP
#define BMMQ_TESTS_VISUAL_TEST_HELPERS_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

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

inline void appendBigEndian32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
}

inline void appendPngChunk(std::vector<uint8_t>& bytes, const char* type, const std::vector<uint8_t>& data)
{
    appendBigEndian32(bytes, static_cast<uint32_t>(data.size()));
    const auto typeOffset = bytes.size();
    bytes.insert(bytes.end(), type, type + 4u);
    bytes.insert(bytes.end(), data.begin(), data.end());
    const auto crc = crc32(0u,
                           bytes.data() + typeOffset,
                           static_cast<uLong>(4u + data.size()));
    appendBigEndian32(bytes, static_cast<uint32_t>(crc));
}

[[nodiscard]] inline std::vector<uint8_t> makeRgbaPng(uint32_t width,
                                                      uint32_t height,
                                                      const std::vector<uint32_t>& argbPixels);

[[nodiscard]] inline std::vector<uint8_t> makeSolidPng2x2Rgba(uint32_t argb)
{
    std::vector<uint32_t> pixels(4u, argb);
    return makeRgbaPng(2u, 2u, pixels);
}

[[nodiscard]] inline std::vector<uint8_t> makeRgbaPng(uint32_t width, uint32_t height, const std::vector<uint32_t>& argbPixels)
{
    const auto expectedPixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    assert(argbPixels.size() == expectedPixels);

    std::vector<uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(width) * 4u + 1u) * static_cast<std::size_t>(height));
    for (std::size_t y = 0; y < height; ++y) {
        raw.push_back(0u);
        for (std::size_t x = 0; x < width; ++x) {
            const auto index = y * static_cast<std::size_t>(width) + x;
            const auto argb = argbPixels[index];
            const auto r = static_cast<uint8_t>((argb >> 16u) & 0xFFu);
            const auto g = static_cast<uint8_t>((argb >> 8u) & 0xFFu);
            const auto b = static_cast<uint8_t>(argb & 0xFFu);
            const auto a = static_cast<uint8_t>((argb >> 24u) & 0xFFu);
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            raw.push_back(a);
        }
    }

    auto compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    const auto status = compress2(compressed.data(),
                                  &compressedSize,
                                  raw.data(),
                                  static_cast<uLong>(raw.size()),
                                  Z_BEST_SPEED);
    if (status != Z_OK) {
        return {};
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    appendBigEndian32(ihdr, width);
    appendBigEndian32(ihdr, height);
    ihdr.push_back(8u);
    ihdr.push_back(6u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    appendPngChunk(png, "IHDR", ihdr);
    appendPngChunk(png, "IDAT", compressed);
    appendPngChunk(png, "IEND", {});
    return png;
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
