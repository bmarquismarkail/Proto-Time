#include <cassert>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

#include "DebugSnapshotManager.hpp"
#include "MemoryStorage.hpp"
#include "MemorySnapshot/SnapshotStorage/SnapshotStorage.h"

int main()
{
    using AddressType = uint16_t;
    using DataType = uint8_t;

    BMMQ::MemoryStorage<AddressType, DataType> store;
    store.addMemBlock(std::make_tuple(
        static_cast<AddressType>(0x0000),
        static_cast<AddressType>(0x0100),
        BMMQ::memAccess::ReadWrite));

    DataType baseValue = 0x42;
    store.write(std::span<const DataType>(&baseValue, 1), static_cast<AddressType>(0x0010));

    BMMQ::SnapshotStorage<AddressType, DataType> snapshot(store);

    DataType observed = 0x00;
    snapshot.read(std::span<DataType>(&observed, 1), static_cast<AddressType>(0x0010));
    assert(observed == baseValue);

    DataType overlayValue = 0x99;
    snapshot.write(std::span<const DataType>(&overlayValue, 1), static_cast<AddressType>(0x0010));

    observed = 0x00;
    snapshot.read(std::span<DataType>(&observed, 1), static_cast<AddressType>(0x0010));
    assert(observed == overlayValue);

    DataType baseObserved = 0x00;
    store.read(std::span<DataType>(&baseObserved, 1), static_cast<AddressType>(0x0010));
    assert(baseObserved == baseValue);

    bool threw = false;
    try {
        store.read(std::span<DataType>(&baseObserved, 1), static_cast<AddressType>(0x0200));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    // Refresh a short low pool after a distant high pool has been added.
    const DataType highBytes[]={0x11,0x22};
    snapshot.write(highBytes,0x80);
    snapshot.write(std::span<const DataType>(&overlayValue,1),0x10);
    std::array<DataType,2> highObserved{};
    snapshot.read(highObserved,0x80);assert(highObserved[0]==0x11&&highObserved[1]==0x22);
    // Random overlapping and descending writes must preserve sparse fallback reads.
    BMMQ::SnapshotStorage<AddressType,DataType> mixed(store);
    std::array<DataType,256> expected{};expected[0x10]=baseValue;
    uint32_t seed=0x12345678;
    for(unsigned step=0;step<600;++step){seed=seed*1664525u+1013904223u;auto address=static_cast<AddressType>((seed>>16)%252);
        std::array<DataType,4> bytes{static_cast<DataType>(seed),static_cast<DataType>(seed>>8),static_cast<DataType>(seed>>16),static_cast<DataType>(seed>>24)};
        mixed.write(bytes,address);std::copy(bytes.begin(),bytes.end(),expected.begin()+address);
        std::array<DataType,256> actual{};mixed.read(actual,0);assert(actual==expected);}

    BMMQ::MemoryStorage<AddressType, DataType> upperStore;
    upperStore.addMemBlock(std::make_tuple(
        static_cast<AddressType>(0xFF00),
        static_cast<AddressType>(0x0100),
        BMMQ::memAccess::ReadWrite));
    BMMQ::SnapshotStorage<AddressType, DataType> upperSnapshot(upperStore);

    DataType lastAddressValue = 0xAB;
    upperSnapshot.write(std::span<const DataType>(&lastAddressValue, 1), static_cast<AddressType>(0xFFFF));

    DataType lastAddressObserved = 0x00;
    upperSnapshot.read(std::span<DataType>(&lastAddressObserved, 1), static_cast<AddressType>(0xFFFF));
    assert(lastAddressObserved == lastAddressValue);

    const DataType boundaryValues[] = {0xCD, 0xEF};
    upperSnapshot.write(std::span<const DataType>(boundaryValues, 2), static_cast<AddressType>(0xFFFE));

    DataType boundaryObserved[] = {0x00, 0x00};
    upperSnapshot.read(std::span<DataType>(boundaryObserved, 2), static_cast<AddressType>(0xFFFE));
    assert(boundaryObserved[0] == boundaryValues[0]);
    assert(boundaryObserved[1] == boundaryValues[1]);

    {
        using LargeAddressType = std::uint64_t;
        BMMQ::MemoryStorage<LargeAddressType, DataType> largeStore;
        const auto hugeBase = static_cast<LargeAddressType>(std::numeric_limits<memindextype<DataType>>::max()) + 8ull;
        largeStore.addMemBlock(std::make_tuple(
            hugeBase,
            static_cast<LargeAddressType>(1),
            BMMQ::memAccess::ReadWrite));

        BMMQ::SnapshotStorage<LargeAddressType, DataType> largeSnapshot(largeStore);
        const DataType hugeValue = 0x5A;
        largeSnapshot.write(std::span<const DataType>(&hugeValue, 1), hugeBase);

        DataType lowObserved = 0x00;
        bool rangeThrew = false;
        try {
            largeSnapshot.read(std::span<DataType>(&lowObserved, 1), static_cast<LargeAddressType>(0));
        } catch (const std::out_of_range&) {
            rangeThrew = true;
        }
        assert(rangeThrew);
    }

    {
        BMMQ::MemoryStorage<AddressType, DataType> invalidStorage;
        BMMQ::MemoryStorage<AddressType, DataType> validStorage;
        validStorage.rehydrate(
            std::vector<DataType>{0x01u},
            std::vector<std::tuple<AddressType, AddressType, BMMQ::memAccess>>{
                {static_cast<AddressType>(0x0000u), static_cast<AddressType>(0x0001u), BMMQ::memAccess::ReadWrite}
            });
        DataType rehydratedObserved = 0x00u;
        validStorage.read(std::span<DataType>(&rehydratedObserved, 1), static_cast<AddressType>(0x0000u));
        assert(rehydratedObserved == 0x01u);

        bool mismatchThrew = false;
        try {
            invalidStorage.rehydrate(
                std::vector<DataType>{0x01u, 0x02u},
                std::vector<std::tuple<AddressType, AddressType, BMMQ::memAccess>>{
                    {static_cast<AddressType>(0x0000u), static_cast<AddressType>(0x0001u), BMMQ::memAccess::ReadWrite}
                });
        } catch (const std::invalid_argument&) {
            mismatchThrew = true;
        }
        assert(mismatchThrew);

        bool invalidAccessThrew = false;
        try {
            invalidStorage.rehydrate(
                std::vector<DataType>{0x01u},
                std::vector<std::tuple<AddressType, AddressType, BMMQ::memAccess>>{
                    {static_cast<AddressType>(0x0000u),
                     static_cast<AddressType>(0x0001u),
                     static_cast<BMMQ::memAccess>(99)}
                });
        } catch (const std::invalid_argument&) {
            invalidAccessThrew = true;
        }
        assert(invalidAccessThrew);
    }

    {
        BMMQ::MemoryStorage<AddressType, DataType> readOnlyStore;
        readOnlyStore.addReadOnlyMem({
            static_cast<AddressType>(0x0040u),
            static_cast<AddressType>(0x0010u)
        });

        BMMQ::MemorySnapshot<AddressType, DataType, uint16_t> readonlySnapshot(readOnlyStore);
        const DataType savedValue = 0x7Du;
        readonlySnapshot.write(std::span<const DataType>(&savedValue, 1), static_cast<AddressType>(0x0042u));

        const auto accessPath = std::filesystem::temp_directory_path() /
            ("proto-time-access-snapshot-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".snap");
        BMMQ::DebugSnapshotManager<AddressType, DataType, uint16_t>::save_to_disk(readonlySnapshot, accessPath);
        auto loaded = BMMQ::DebugSnapshotManager<AddressType, DataType, uint16_t>::load_snapshot(accessPath);

        std::error_code ec;
        std::filesystem::remove(accessPath, ec);
        assert(loaded.storage.accessAt(static_cast<AddressType>(0x0042u)) == BMMQ::memAccess::Read);
    }

    {
        const auto malformedPath = std::filesystem::temp_directory_path() /
            ("proto-time-malformed-snapshot-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".snap");
        {
            std::ofstream output(malformedPath, std::ios::binary | std::ios::trunc);
            assert(output);
            output.write("SNAP", 4);
            const uint32_t version = 1;
            const uint32_t coreId = BMMQ::DebugSnapshotManager<AddressType, uint16_t, uint16_t>::CORE_GAMEBOY;
            const uint64_t regCount = 0;
            const uint64_t poolCount = 0;
            const uint64_t malformedMemSize = 1;
            output.write(reinterpret_cast<const char*>(&version), sizeof(version));
            output.write(reinterpret_cast<const char*>(&coreId), sizeof(coreId));
            output.write(reinterpret_cast<const char*>(&regCount), sizeof(regCount));
            output.write(reinterpret_cast<const char*>(&poolCount), sizeof(poolCount));
            output.write(reinterpret_cast<const char*>(&malformedMemSize), sizeof(malformedMemSize));
        }

        bool malformedThrew = false;
        try {
            (void)BMMQ::DebugSnapshotManager<AddressType, uint16_t, uint16_t>::load_from_disk(malformedPath);
        } catch (const std::runtime_error&) {
            malformedThrew = true;
        }
        std::error_code ec;
        std::filesystem::remove(malformedPath, ec);
        assert(malformedThrew);
    }

    return 0;
}
