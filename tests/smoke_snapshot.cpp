#include <cassert>
#include <cstdint>
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

    return 0;
}
