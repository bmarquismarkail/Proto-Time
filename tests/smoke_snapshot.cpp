#include <cassert>
#include <cstdint>

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
        BMMQ::MEM_READ_WRITE));

    DataType baseValue = 0x42;
    store.write(&baseValue, static_cast<AddressType>(0x0010), static_cast<AddressType>(1));

    BMMQ::SnapshotStorage<AddressType, DataType> snapshot(store);

    DataType observed = 0x00;
    snapshot.read(&observed, static_cast<AddressType>(0x0010), static_cast<AddressType>(1));
    assert(observed == baseValue);

    DataType overlayValue = 0x99;
    snapshot.write(&overlayValue, static_cast<AddressType>(0x0010), static_cast<AddressType>(1));

    observed = 0x00;
    snapshot.read(&observed, static_cast<AddressType>(0x0010), static_cast<AddressType>(1));
    assert(observed == overlayValue);

    DataType baseObserved = 0x00;
    store.read(&baseObserved, static_cast<AddressType>(0x0010), static_cast<AddressType>(1));
    assert(baseObserved == baseValue);

    return 0;
}
