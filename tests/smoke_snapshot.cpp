#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

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

    return 0;
}
