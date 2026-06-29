#include "SaveState.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace BMMQ {

namespace {
constexpr std::size_t kChunkNameSize = 32u;
constexpr std::size_t kMaxChunkSize = 64u * 1024u * 1024u;

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

std::uint32_t readU32(std::istream& in, const char* fieldName)
{
    std::array<std::uint8_t, 4> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error(std::string("Failed to read save state ") + fieldName);
    }
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void readExact(std::istream& in, char* data, std::size_t size, const char* fieldName)
{
    in.read(data, static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error(std::string("Failed to read save state ") + fieldName);
    }
}

void writeHeaderBytes(std::vector<std::uint8_t>& out, const SaveStateHeader& header)
{
    out.insert(out.end(), header.magic, header.magic + sizeof(header.magic));
    appendU32(out, header.version);
    appendU32(out, header.core_id);
    appendU32(out, header.rom_hash);
    appendU32(out, static_cast<std::uint32_t>(header.checksum));
    appendU32(out, header.chunk_count);
}

void writeChunkBytes(std::vector<std::uint8_t>& out, const SaveStateChunk& chunk)
{
    if (chunk.size != chunk.data.size()) {
        throw std::runtime_error("Chunk size does not match payload length");
    }
    if (chunk.name.size() > kChunkNameSize) {
        throw std::runtime_error("Chunk name exceeds 32 bytes");
    }

    appendU32(out, chunk.size);
    std::array<char, kChunkNameSize> encodedName{};
    std::memcpy(encodedName.data(), chunk.name.data(), chunk.name.size());
    out.insert(out.end(), encodedName.begin(), encodedName.end());
    out.insert(out.end(), chunk.data.begin(), chunk.data.end());
}
} // namespace

// Simple CRC32 implementation.
uint32_t crc32(const uint8_t* data, std::size_t size) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xEDB88320u : crc >> 1u;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

SaveStateFile SaveStateReader::read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open save state file: " + path.string());
    }

    SaveStateFile state;
    readExact(file, state.header.magic, sizeof(state.header.magic), "magic");
    if (std::memcmp(state.header.magic, kSaveStateMagic, sizeof(state.header.magic)) != 0) {
        throw std::runtime_error("Invalid save state magic");
    }

    state.header.version = readU32(file, "version");
    state.header.core_id = readU32(file, "core id");
    state.header.rom_hash = readU32(file, "ROM hash");
    state.header.checksum = static_cast<SaveStateChecksum>(readU32(file, "checksum mode"));
    state.header.chunk_count = readU32(file, "chunk count");

    if (state.header.version != kSaveStateVersion) {
        throw std::runtime_error("Unsupported save state version: " + std::to_string(state.header.version));
    }
    if (state.header.checksum != SaveStateChecksum::None &&
        state.header.checksum != SaveStateChecksum::Crc32) {
        throw std::runtime_error("Unsupported save state checksum mode");
    }

    std::vector<std::uint8_t> payload;
    writeHeaderBytes(payload, state.header);

    state.chunks.reserve(state.header.chunk_count);
    for (std::uint32_t i = 0; i < state.header.chunk_count; ++i) {
        SaveStateChunk chunk;
        chunk.size = readU32(file, "chunk size");
        if (chunk.size > kMaxChunkSize) {
            throw std::runtime_error("Save state chunk is too large");
        }

        std::array<char, kChunkNameSize> encodedName{};
        readExact(file, encodedName.data(), encodedName.size(), "chunk name");
        const auto nameEnd = std::find(encodedName.begin(), encodedName.end(), '\0');
        chunk.name.assign(encodedName.begin(), nameEnd);

        chunk.data.resize(chunk.size);
        if (!chunk.data.empty()) {
            readExact(file,
                      reinterpret_cast<char*>(chunk.data.data()),
                      chunk.data.size(),
                      "chunk data");
        }

        writeChunkBytes(payload, chunk);
        state.chunks.push_back(std::move(chunk));
    }

    if (state.header.checksum == SaveStateChecksum::Crc32) {
        state.checksum = readU32(file, "checksum");
        const auto computed = crc32(payload.data(), payload.size());
        if (computed != state.checksum) {
            throw std::runtime_error("Save state checksum mismatch");
        }
    }

    if (file.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Save state contains trailing data");
    }
    if (file.bad()) {
        throw std::runtime_error("Failed to finalize save state read");
    }

    return state;
}

SaveStateFile SaveStateReader::readForCore(const std::filesystem::path& path,
                                          uint32_t expectedCoreId,
                                          std::span<const uint8_t> expectedRomBytes)
{
    if (expectedCoreId == 0u) {
        throw std::invalid_argument("expected core id must be set");
    }

    auto state = read(path);
    if (state.header.core_id != expectedCoreId) {
        throw std::invalid_argument("save state is not compatible with this machine");
    }
    const auto expectedRomHash = crc32(expectedRomBytes.data(), expectedRomBytes.size());
    if (state.header.rom_hash != 0u && expectedRomHash != state.header.rom_hash) {
        throw std::invalid_argument("save state ROM identity does not match the loaded ROM");
    }
    return state;
}

void SaveStateReader::write(const SaveStateFile& state, const std::filesystem::path& path)
{
    SaveStateHeader header = state.header;
    std::memcpy(header.magic, kSaveStateMagic, sizeof(header.magic));
    header.version = kSaveStateVersion;
    if (state.chunks.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Too many save state chunks");
    }
    header.chunk_count = static_cast<std::uint32_t>(state.chunks.size());

    std::vector<std::uint8_t> payload;
    writeHeaderBytes(payload, header);
    for (const auto& chunk : state.chunks) {
        writeChunkBytes(payload, chunk);
    }

    std::vector<std::uint8_t> trailer;
    if (header.checksum == SaveStateChecksum::Crc32) {
        const auto checksum = crc32(payload.data(), payload.size());
        appendU32(trailer, checksum);
    }

    const auto tempPath = path.string() + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open temp save state file: " + tempPath);
        }
        if (!payload.empty()) {
            file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        }
        if (!trailer.empty()) {
            file.write(reinterpret_cast<const char*>(trailer.data()), static_cast<std::streamsize>(trailer.size()));
        }
        if (!file) {
            throw std::runtime_error("Failed to write save state file: " + tempPath);
        }
        file.close();
        if (!file) {
            throw std::runtime_error("Failed to close temp save state file: " + tempPath);
        }
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::error_code ignoreRemoveError;
        std::filesystem::remove(tempPath, ignoreRemoveError);
        throw std::runtime_error("Failed to rename save state into place: " + ec.message());
    }
}

} // namespace BMMQ
