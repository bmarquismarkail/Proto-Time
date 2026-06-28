#ifndef BMMQ_SAVESTATE_HPP
#define BMMQ_SAVESTATE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace BMMQ {

// Compute CRC32 checksum over a byte buffer.
// Defined in SaveState.cpp; declared here for inter-translation-unit use.
uint32_t crc32(const uint8_t* data, std::size_t size) noexcept;

// Magic bytes identifying a Proto-Time save state file.
static constexpr const char kSaveStateMagic[] = "PTIME";

// Save state format version.
static constexpr uint32_t kSaveStateVersion = 1;

// Core identifiers.
static constexpr uint32_t kCoreId_GameBoy = 0x01u;
static constexpr uint32_t kCoreId_GameGear = 0x02u;

// Checksum type.
enum class SaveStateChecksum : uint32_t {
    None = 0,
    Crc32 = 1,
};

// Save state header.
struct SaveStateHeader {
    char magic[5]{};
    uint32_t version = 0;
    uint32_t core_id = 0;
    uint32_t rom_hash = 0;
    SaveStateChecksum checksum = SaveStateChecksum::None;
    uint32_t chunk_count = 0;
};

// Save state chunk.
struct SaveStateChunk {
    std::string name;
    uint32_t size = 0;
    std::vector<uint8_t> data;
};

// Save state file.
struct SaveStateFile {
    SaveStateHeader header;
    std::vector<SaveStateChunk> chunks;
    uint32_t checksum = 0;
};

// Save state reader.
class SaveStateReader {
public:
    // Read a save state file.
    static SaveStateFile read(const std::filesystem::path& path);

    // Write a save state file.
    static void write(const SaveStateFile& state, const std::filesystem::path& path);

private:
    SaveStateReader() = default;
};

} // namespace BMMQ

#endif // BMMQ_SAVESTATE_HPP
