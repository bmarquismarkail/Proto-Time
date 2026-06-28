#ifndef DEBUG_SNAPSHOT_MANAGER_HPP
#define DEBUG_SNAPSHOT_MANAGER_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "MemorySnapshot/MemorySnapshot.hpp"
#include "MemoryStorage.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
class DebugSnapshotManager {
public:
    static constexpr uint32_t CORE_GAMEBOY = 1;
    static constexpr uint32_t CORE_GAMEGEAR = 2;
    static constexpr uint32_t SNAPSHOT_VERSION = 1;
    static constexpr std::size_t MAX_SNAPSHOT_MEMORY_BYTES = 64u * 1024u * 1024u;

    struct RegisterSnapshot {
        std::string name;
        uint8_t width = 0;
        uint8_t storage = 0;
        uint8_t isPair = 0;
        RegType value{};
    };

    struct LoadedSnapshot {
        MemoryStorage<AddressType, DataType> storage;
        std::vector<RegisterSnapshot> registers;
        uint32_t coreId = 0;
    };

    struct PoolSnapshot {
        AddressType start{};
        std::size_t offset = 0;
        memAccess access = memAccess::ReadWrite;
    };

    static void save_to_disk(MemorySnapshot<AddressType, DataType, RegType>& snapshot,
                             const std::filesystem::path& path,
                             uint32_t coreId = CORE_GAMEBOY)
    {
        const auto& data = snapshot.mem.data();
        if (data.size() > std::numeric_limits<uint64_t>::max() / sizeof(DataType)) {
            throw std::runtime_error("Snapshot memory payload is too large.");
        }
        const auto memSize = static_cast<uint64_t>(data.size()) * sizeof(DataType);
        if (memSize > static_cast<uint64_t>(MAX_SNAPSHOT_MEMORY_BYTES) ||
            memSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            throw std::runtime_error("Snapshot memory payload is too large.");
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            throw std::runtime_error("Failed to open save path: " + path.string());
        }

        const auto writeOrThrow = [&ofs, &path](const char* data, std::streamsize size) {
            ofs.write(data, size);
            if (!ofs) {
                throw std::runtime_error("Failed to write snapshot: " + path.string());
            }
        };

        writeOrThrow("SNAP", 4);
        const uint32_t version = SNAPSHOT_VERSION;
        writeOrThrow(reinterpret_cast<const char*>(&version), sizeof(version));
        writeOrThrow(reinterpret_cast<const char*>(&coreId), sizeof(coreId));

        const uint64_t regCount = snapshot.file.entries().size();
        writeOrThrow(reinterpret_cast<const char*>(&regCount), sizeof(regCount));
        for (const auto& entry : snapshot.file.entries()) {
            const auto nameLen = static_cast<uint32_t>(entry.name.size());
            writeOrThrow(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
            writeOrThrow(entry.name.data(), static_cast<std::streamsize>(nameLen));

            const auto width = static_cast<uint8_t>(entry.descriptor.width);
            const auto storage = static_cast<uint8_t>(entry.descriptor.storage);
            const auto isPair = static_cast<uint8_t>(entry.descriptor.isPair ? 1u : 0u);
            writeOrThrow(reinterpret_cast<const char*>(&width), sizeof(width));
            writeOrThrow(reinterpret_cast<const char*>(&storage), sizeof(storage));
            writeOrThrow(reinterpret_cast<const char*>(&isPair), sizeof(isPair));
            writeOrThrow(reinterpret_cast<const char*>(&entry.reg->value), sizeof(RegType));
        }

        const auto& pools = snapshot.mem.pools();
        const uint64_t poolCount = pools.size();
        writeOrThrow(reinterpret_cast<const char*>(&poolCount), sizeof(poolCount));
        for (const auto& pool : pools) {
            const auto offset = static_cast<uint64_t>(pool.second);
            const auto access = static_cast<uint8_t>(snapshot.mem.accessAt(pool.first));
            writeOrThrow(reinterpret_cast<const char*>(&pool.first), sizeof(AddressType));
            writeOrThrow(reinterpret_cast<const char*>(&offset), sizeof(offset));
            writeOrThrow(reinterpret_cast<const char*>(&access), sizeof(access));
        }

        writeOrThrow(reinterpret_cast<const char*>(&memSize), sizeof(memSize));
        if (memSize != 0u) {
            writeOrThrow(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(memSize));
        }
        ofs.close();
        if (!ofs) {
            throw std::runtime_error("Failed to finalize snapshot: " + path.string());
        }
    }

    static LoadedSnapshot load_snapshot(const std::filesystem::path& path, uint32_t expectedCoreId = CORE_GAMEBOY)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            throw std::runtime_error("Failed to open snapshot path: " + path.string());
        }

        const auto readOrThrow = [&ifs, &path](char* data, std::streamsize size) {
            ifs.read(data, size);
            if (ifs.eof()) {
                throw std::runtime_error("Failed to read snapshot: " + path.string());
            }
            if (!ifs) {
                throw std::runtime_error("Failed to read snapshot: " + path.string());
            }
        };

        char magic[4]{};
        readOrThrow(magic, sizeof(magic));
        if (std::string(magic, sizeof(magic)) != "SNAP") {
            throw std::runtime_error("Invalid snapshot format.");
        }

        uint32_t version = 0;
        readOrThrow(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != SNAPSHOT_VERSION) {
            throw std::runtime_error("Unsupported snapshot version.");
        }

        uint32_t coreId = 0;
        readOrThrow(reinterpret_cast<char*>(&coreId), sizeof(coreId));
        if (coreId != expectedCoreId) {
            throw std::runtime_error("Snapshot core does not match expected core.");
        }

        uint64_t regCount = 0;
        readOrThrow(reinterpret_cast<char*>(&regCount), sizeof(regCount));
        if (regCount > 1024u) {
            throw std::runtime_error("Snapshot register count is too large.");
        }
        std::vector<RegisterSnapshot> registers;
        registers.reserve(static_cast<std::size_t>(regCount));
        for (uint64_t i = 0; i < regCount; ++i) {
            uint32_t nameLen = 0;
            readOrThrow(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
            if (nameLen > 256u) {
                throw std::runtime_error("Snapshot register name is too long.");
            }

            RegisterSnapshot reg;
            reg.name.resize(nameLen);
            if (nameLen != 0u) {
                readOrThrow(reg.name.data(), static_cast<std::streamsize>(nameLen));
            }
            readOrThrow(reinterpret_cast<char*>(&reg.width), sizeof(reg.width));
            readOrThrow(reinterpret_cast<char*>(&reg.storage), sizeof(reg.storage));
            readOrThrow(reinterpret_cast<char*>(&reg.isPair), sizeof(reg.isPair));
            if (reg.isPair > 1u) {
                throw std::runtime_error("Snapshot register pair flag is invalid.");
            }
            readOrThrow(reinterpret_cast<char*>(&reg.value), sizeof(RegType));
            registers.push_back(std::move(reg));
        }

        uint64_t poolCount = 0;
        readOrThrow(reinterpret_cast<char*>(&poolCount), sizeof(poolCount));
        if (poolCount > 65536u) {
            throw std::runtime_error("Snapshot pool count is too large.");
        }
        std::vector<PoolSnapshot> pools;
        pools.reserve(static_cast<std::size_t>(poolCount));
        for (uint64_t i = 0; i < poolCount; ++i) {
            AddressType start{};
            uint64_t offset = 0;
            uint8_t access = 0;
            readOrThrow(reinterpret_cast<char*>(&start), sizeof(start));
            readOrThrow(reinterpret_cast<char*>(&offset), sizeof(offset));
            readOrThrow(reinterpret_cast<char*>(&access), sizeof(access));
            if (offset > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
                throw std::runtime_error("Snapshot pool offset is too large.");
            }
            if (!pools.empty() && offset < pools.back().offset) {
                throw std::runtime_error("Snapshot pool offsets are not monotonic.");
            }
            if (!isValidRehydrateAccess(static_cast<memAccess>(access))) {
                throw std::runtime_error("Snapshot pool access is invalid.");
            }
            pools.push_back(PoolSnapshot{
                start,
                static_cast<std::size_t>(offset),
                static_cast<memAccess>(access)
            });
        }

        uint64_t memSize = 0;
        readOrThrow(reinterpret_cast<char*>(&memSize), sizeof(memSize));
        if (memSize % sizeof(DataType) != 0u) {
            throw std::runtime_error("Invalid snapshot memory size.");
        }
        if (memSize > static_cast<uint64_t>(MAX_SNAPSHOT_MEMORY_BYTES) ||
            memSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
            memSize > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("Snapshot memory payload is too large.");
        }
        std::vector<DataType> newMem(static_cast<std::size_t>(memSize / sizeof(DataType)));
        if (memSize != 0u) {
            readOrThrow(reinterpret_cast<char*>(newMem.data()), static_cast<std::streamsize>(memSize));
        }
        if (pools.empty() && !newMem.empty()) {
            throw std::runtime_error("Snapshot memory data has no pool map.");
        }

        std::vector<std::tuple<AddressType, AddressType, memAccess>> newMap;
        newMap.reserve(pools.size());
        for (std::size_t i = 0; i < pools.size(); ++i) {
            const auto start = pools[i].start;
            const auto offset = pools[i].offset;
            const auto nextOffset = (i + 1u < pools.size()) ? pools[i + 1u].offset : newMem.size();
            if (offset >= nextOffset || nextOffset > newMem.size()) {
                throw std::runtime_error("Invalid snapshot pool offsets.");
            }
            const auto segmentLength = nextOffset - offset;
            if (segmentLength > static_cast<std::size_t>(std::numeric_limits<AddressType>::max())) {
                throw std::runtime_error("Snapshot pool length exceeds address width.");
            }
            const auto length = static_cast<AddressType>(segmentLength);
            newMap.emplace_back(start, length, pools[i].access);
        }

        LoadedSnapshot loaded;
        loaded.coreId = coreId;
        loaded.registers = std::move(registers);
        loaded.storage.rehydrate(std::move(newMem), std::move(newMap));
        if (ifs.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Snapshot contains trailing data.");
        }
        if (ifs.bad()) {
            throw std::runtime_error("Failed to finalize snapshot read: " + path.string());
        }
        return loaded;
    }

    static MemoryStorage<AddressType, DataType> load_from_disk(
        const std::filesystem::path& path,
        uint32_t expectedCoreId = CORE_GAMEBOY)
    {
        auto loaded = load_snapshot(path, expectedCoreId);
        return std::move(loaded.storage);
    }
};

} // namespace BMMQ

#endif // DEBUG_SNAPSHOT_MANAGER_HPP
