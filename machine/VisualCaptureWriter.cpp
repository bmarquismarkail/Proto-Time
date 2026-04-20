#include "VisualCaptureWriter.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <span>
#include <string>
#include <vector>

#include <zlib.h>

namespace BMMQ {
namespace {

[[nodiscard]] std::string jsonEscaped(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (byte < 0x20u) {
                constexpr char kHex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(kHex[(byte >> 4u) & 0x0Fu]);
                escaped.push_back(kHex[byte & 0x0Fu]);
            } else {
                escaped.push_back(c);
            }
            break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string fixedHex(uint32_t value, int width)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(width) << value;
    return out.str();
}

void appendBigEndian32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void appendPngChunk(std::vector<uint8_t>& png, const std::array<char, 4>& type, const std::vector<uint8_t>& data)
{
    appendBigEndian32(png, static_cast<uint32_t>(data.size()));
    const auto typeOffset = png.size();
    for (const auto c : type) {
        png.push_back(static_cast<uint8_t>(c));
    }
    png.insert(png.end(), data.begin(), data.end());
    const auto crc = crc32(0u, png.data() + typeOffset, static_cast<uInt>(type.size() + data.size()));
    appendBigEndian32(png, static_cast<uint32_t>(crc));
}

} // namespace

bool VisualCaptureWriter::writeManifests(const std::filesystem::path& captureDirectory,
                                         std::string_view machineId,
                                         const std::vector<VisualCaptureEntry>& entries,
                                         std::string& error)
{
    const auto writeRules = [&](const std::filesystem::path& path, bool includeMetadata) {
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << "{\n";
        out << "  \"schemaVersion\": 1,\n";
        out << "  \"id\": \"capture." << jsonEscaped(machineId) << "\",\n";
        out << "  \"name\": \"Captured " << jsonEscaped(machineId) << " visual resources\",\n";
        out << "  \"targets\": [\"" << jsonEscaped(machineId) << "\"],\n";
        out << "  \"rules\": [\n";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            out << "    {\n";
            out << "      \"match\": {\n";
            out << "        \"kind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
            out << "        \"sourceHash\": \"" << toHexVisualHash(entry.descriptor.sourceHash) << "\",\n";
            out << "        \"sourceBank\": " << entry.descriptor.source.bank << ",\n";
            out << "        \"sourceAddress\": \"" << fixedHex(entry.descriptor.source.address, 4) << "\",\n";
            out << "        \"tileIndex\": " << entry.descriptor.source.index << ",\n";
            out << "        \"paletteRegister\": \"" << jsonEscaped(entry.descriptor.source.paletteRegister) << "\",\n";
            out << "        \"paletteValue\": \"" << fixedHex(entry.descriptor.source.paletteValue, 2) << "\",\n";
            out << "        \"decodedHash\": \"" << toHexVisualHash(entry.descriptor.contentHash) << "\",\n";
            out << "        \"paletteHash\": \"" << toHexVisualHash(entry.descriptor.paletteHash) << "\",\n";
            out << "        \"paletteAwareHash\": \"" << toHexVisualHash(entry.descriptor.paletteAwareHash) << "\",\n";
            out << "        \"width\": " << entry.descriptor.width << ",\n";
            out << "        \"height\": " << entry.descriptor.height << "\n";
            out << "      },\n";
            if (includeMetadata) {
                out << "      \"metadata\": {\n";
                out << "        \"resourceKind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
                out << "        \"sourceBank\": " << entry.descriptor.source.bank << ",\n";
                out << "        \"sourceAddress\": \"" << fixedHex(entry.descriptor.source.address, 4) << "\",\n";
                out << "        \"tileIndex\": " << entry.descriptor.source.index << ",\n";
                out << "        \"paletteRegister\": \"" << jsonEscaped(entry.descriptor.source.paletteRegister) << "\",\n";
                out << "        \"paletteValue\": \"" << fixedHex(entry.descriptor.source.paletteValue, 2) << "\"\n";
                out << "      },\n";
            }
            out << "      \"replace\": {\n";
            out << "        \"image\": \"" << jsonEscaped(entry.imagePath) << "\"\n";
            out << "      }\n";
            out << "    }" << (i + 1u < entries.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    };

    const auto writeMetadata = [&](const std::filesystem::path& path) {
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << "{\n";
        out << "  \"schemaVersion\": 1,\n";
        out << "  \"machine\": \"" << jsonEscaped(machineId) << "\",\n";
        out << "  \"entries\": [\n";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            out << "    {\n";
            out << "      \"image\": \"" << jsonEscaped(entry.imagePath) << "\",\n";
            out << "      \"resourceKind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
            out << "      \"sourceHash\": \"" << toHexVisualHash(entry.descriptor.sourceHash) << "\",\n";
            out << "      \"decodedHash\": \"" << toHexVisualHash(entry.descriptor.contentHash) << "\",\n";
            out << "      \"paletteHash\": \"" << toHexVisualHash(entry.descriptor.paletteHash) << "\",\n";
            out << "      \"paletteAwareHash\": \"" << toHexVisualHash(entry.descriptor.paletteAwareHash) << "\",\n";
            out << "      \"sourceBank\": " << entry.descriptor.source.bank << ",\n";
            out << "      \"sourceAddress\": \"" << fixedHex(entry.descriptor.source.address, 4) << "\",\n";
            out << "      \"tileIndex\": " << entry.descriptor.source.index << ",\n";
            out << "      \"paletteRegister\": \"" << jsonEscaped(entry.descriptor.source.paletteRegister) << "\",\n";
            out << "      \"paletteValue\": \"" << fixedHex(entry.descriptor.source.paletteValue, 2) << "\"\n";
            out << "    }" << (i + 1u < entries.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    };

    if (!writeRules(captureDirectory / "pack.json", false) ||
        !writeRules(captureDirectory / "manifest.stub.json", true) ||
        !writeMetadata(captureDirectory / "capture_metadata.json")) {
        error = "unable to write visual capture manifest";
        return false;
    }
    return true;
}

bool VisualCaptureWriter::writeDecodedResourcePng(const std::filesystem::path& path,
                                                  const DecodedVisualResource& resource,
                                                  std::string& error)
{
    constexpr std::array<uint8_t, 4> kIndexedArgbAlpha{
        0xFFu, 0xFFu, 0xFFu, 0xFFu,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbRed{
        0xE0u, 0x88u, 0x34u, 0x08u,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbGreen{
        0xF8u, 0xC0u, 0x68u, 0x18u,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbBlue{
        0xD0u, 0x70u, 0x56u, 0x20u,
    };

    const auto width = resource.descriptor.width;
    const auto height = resource.descriptor.height;
    if (width == 0u || height == 0u || resource.stride == 0u) {
        error = "invalid resource dimensions or stride: width=" + std::to_string(width) +
            " height=" + std::to_string(height) +
            " stride=" + std::to_string(resource.stride);
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(width) * 4u + 1u) * static_cast<std::size_t>(height));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0u);
        for (uint32_t x = 0; x < width; ++x) {
            const auto sourceIndex = static_cast<std::size_t>(y) * resource.stride + x;
            const auto color = sourceIndex < resource.pixels.size() ? resource.pixels[sourceIndex] & 0x03u : 0u;
            raw.push_back(kIndexedArgbRed[color]);
            raw.push_back(kIndexedArgbGreen[color]);
            raw.push_back(kIndexedArgbBlue[color]);
            raw.push_back(kIndexedArgbAlpha[color]);
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        error = "unable to compress visual capture PNG";
        return false;
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
    appendPngChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
    appendPngChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendPngChunk(png, {'I', 'E', 'N', 'D'}, {});

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "unable to write visual capture image";
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

} // namespace BMMQ
