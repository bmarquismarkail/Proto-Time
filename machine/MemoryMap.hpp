#ifndef BMMQ_MEMORY_MAP_HPP
#define BMMQ_MEMORY_MAP_HPP

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "../memory/MemoryStorage.hpp"

namespace BMMQ {

class MemoryMap {
public:
    using AddressType = uint16_t;
    using DataType = uint8_t;

    void reset() {
        storage_ = {};
    }

    void mapRange(AddressType base, AddressType length, memAccess access) {
        storage_.addMemBlock({base, length, access});
    }

    void mapRom(AddressType base, AddressType length) {
        mapRange(base, length, memAccess::Read);
    }

    void mapRam(AddressType base, AddressType length) {
        mapRange(base, length, memAccess::ReadWrite);
    }

    void installRom(const std::vector<DataType>& bytes, AddressType base) {
        storage_.load(std::span<const DataType>(bytes.data(), bytes.size()), base);
    }

    [[nodiscard]] DataType read8(AddressType address) const {
        DataType value = 0;
        storage_.read(std::span<DataType>(&value, 1), address);
        return value;
    }

    void write8(AddressType address, DataType value) {
        storage_.write(std::span<const DataType>(&value, 1), address);
    }

    [[nodiscard]] MemoryStorage<AddressType, DataType>& storage() {
        return storage_;
    }

    [[nodiscard]] const MemoryStorage<AddressType, DataType>& storage() const {
        return storage_;
    }

private:
    MemoryStorage<AddressType, DataType> storage_;
};

} // namespace BMMQ

#endif // BMMQ_MEMORY_MAP_HPP
