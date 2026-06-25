#include "../MemoryStorage.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace BMMQ {

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::addMemBlock(
    std::tuple<AddressType, AddressType, memAccess> memBlock)
{
    map.push_back(memBlock);
    mem.resize(mem.size() + std::get<1>(memBlock));
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::addReadOnlyMem(
    std::pair<AddressType, AddressType> romBlock)
{
    addMemBlock({romBlock.first, romBlock.second, memAccess::Read});
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::addWriteOnlyMem(
    std::pair<AddressType, AddressType> womBlock)
{
    addMemBlock({womBlock.first, womBlock.second, memAccess::Write});
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::addReadWriteMem(
    std::pair<AddressType, AddressType> block)
{
    addMemBlock({block.first, block.second, memAccess::ReadWrite});
}

template<typename AddressType, typename DataType>
std::span<const DataType> MemoryStorage<AddressType, DataType>::readableSpan(
    AddressType address,
    std::size_t count) const
{
    if (count == 0) return {};

    std::size_t offset = 0;
    for (const auto& entry : map) {
        const auto base = std::get<0>(entry);
        const auto length = std::get<1>(entry);
        const auto access = std::get<2>(entry);
        if (base <= address && static_cast<AddressType>(address - base) < length) {
            if (!hasAccess(access, memAccess::Read)) {
                throw std::out_of_range("address is not readable");
            }

            const auto localOffset = static_cast<std::size_t>(address - base);
            const auto available = static_cast<std::size_t>(length) - localOffset;
            if (count > available) {
                std::ostringstream oss;
                oss << "read extends past mapped range at 0x" << std::hex
                    << static_cast<unsigned long long>(address)
                    << " count=0x" << count;
                throw std::out_of_range(oss.str());
            }

            return std::span<const DataType>(mem).subspan(offset + localOffset, count);
        }
        offset += static_cast<std::size_t>(length);
    }

    throw std::out_of_range("address is not mapped");
}

template<typename AddressType, typename DataType>
std::span<DataType> MemoryStorage<AddressType, DataType>::writableSpan(
    AddressType address,
    std::size_t count)
{
    if (count == 0) return {};

    std::size_t offset = 0;
    for (const auto& entry : map) {
        const auto base = std::get<0>(entry);
        const auto length = std::get<1>(entry);
        const auto access = std::get<2>(entry);
        if (base <= address && static_cast<AddressType>(address - base) < length) {
            if (!hasAccess(access, memAccess::Write)) {
                throw std::out_of_range("address is not writable");
            }

            const auto localOffset = static_cast<std::size_t>(address - base);
            const auto available = static_cast<std::size_t>(length) - localOffset;
            if (count > available) {
                std::ostringstream oss;
                oss << "write extends past mapped range at 0x" << std::hex
                    << static_cast<unsigned long long>(address)
                    << " count=0x" << count;
                throw std::out_of_range(oss.str());
            }

            return std::span<DataType>(mem).subspan(offset + localOffset, count);
        }
        offset += static_cast<std::size_t>(length);
    }

    throw std::out_of_range("address is not mapped");
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::read(std::span<DataType> stream, AddressType address) const
{
    if (stream.empty()) return;
    if (addressTranslator_) {
        address = addressTranslator_(address);
    }
    if (readInterceptor_ && readInterceptor_(address, stream)) {
        return;
    }
    const auto src = readableSpan(address, stream.size());
    std::copy(src.begin(), src.end(), stream.begin());
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::write(std::span<const DataType> value, AddressType address)
{
    if (value.empty()) return;
    if (addressTranslator_) {
        address = addressTranslator_(address);
    }
    if (writeInterceptor_ && writeInterceptor_(address, value)) {
        return;
    }

    std::size_t mapOffset = 0;
    std::size_t valueOffset = 0;
    bool foundStart = false;

    for (const auto& entry : map) {
        const auto base = std::get<0>(entry);
        const auto length = std::get<1>(entry);
        const auto access = std::get<2>(entry);
        const auto localOffset = foundStart ? 0 : static_cast<std::size_t>(address - base);
        const auto startsInBlock = base <= address && static_cast<AddressType>(address - base) < length;
        const auto continuesInBlock = foundStart && address == base;

        if (!startsInBlock && !continuesInBlock) {
            mapOffset += static_cast<std::size_t>(length);
            continue;
        }

        foundStart = true;
        if (!hasAccess(access, memAccess::Write)) {
            throw std::out_of_range("address is not writable");
        }

        const auto available = static_cast<std::size_t>(length) - localOffset;
        const auto chunkSize = std::min(available, value.size() - valueOffset);
        auto dst = std::span<DataType>(mem).subspan(mapOffset + localOffset, chunkSize);
        std::copy_n(value.begin() + static_cast<std::ptrdiff_t>(valueOffset), chunkSize, dst.begin());
        valueOffset += chunkSize;
        if (valueOffset == value.size()) {
            return;
        }

        address = static_cast<AddressType>(base + length);
        mapOffset += static_cast<std::size_t>(length);
    }

    std::ostringstream oss;
    if (foundStart) {
        oss << "write extends past mapped range at 0x" << std::hex << static_cast<unsigned long long>(address)
            << " count=0x" << value.size();
    } else {
        oss << "address is not mapped for write at 0x" << std::hex << static_cast<unsigned long long>(address)
            << " count=0x" << value.size();
    }
    throw std::out_of_range(oss.str());
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::load(std::span<const DataType> value, AddressType address)
{
    if (value.empty()) return;

    std::size_t mapOffset = 0;
    std::size_t valueOffset = 0;
    bool foundStart = false;

    for (const auto& entry : map) {
        const auto base = std::get<0>(entry);
        const auto length = std::get<1>(entry);
        const auto localOffset = foundStart ? 0 : static_cast<std::size_t>(address - base);
        const auto startsInBlock = base <= address && static_cast<AddressType>(address - base) < length;
        const auto continuesInBlock = foundStart && address == base;

        if (!startsInBlock && !continuesInBlock) {
            mapOffset += static_cast<std::size_t>(length);
            continue;
        }

        foundStart = true;
        const auto available = static_cast<std::size_t>(length) - localOffset;
        const auto chunkSize = std::min(available, value.size() - valueOffset);
        auto dst = std::span<DataType>(mem).subspan(mapOffset + localOffset, chunkSize);
        std::copy_n(value.begin() + static_cast<std::ptrdiff_t>(valueOffset), chunkSize, dst.begin());
        valueOffset += chunkSize;
        if (valueOffset == value.size()) {
            return;
        }

        address = static_cast<AddressType>(base + length);
        mapOffset += static_cast<std::size_t>(length);
    }

    throw std::out_of_range(foundStart ? "load extends past mapped range" : "address is not mapped");
}

template<typename AddressType, typename DataType>
memAccess MemoryStorage<AddressType, DataType>::accessAt(AddressType address) const
{
    for (const auto& block : map) {
        const auto& [base, length, access] = block;
        if (base <= address && static_cast<AddressType>(address - base) < length) {
            return access;
        }
    }
    return memAccess::Unmapped;
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType, DataType>::rehydrate(
    std::vector<DataType> newMem,
    std::vector<std::tuple<AddressType, AddressType, memAccess>> newMap)
{
    std::size_t mappedSize = 0;
    for (const auto& entry : newMap) {
        const auto base = std::get<0>(entry);
        const auto length = std::get<1>(entry);
        const auto access = std::get<2>(entry);
        if (!isValidMemoryAccess(access)) {
            throw std::invalid_argument("rehydrate received an invalid access flag");
        }
        if constexpr (std::is_signed_v<AddressType>) {
            if (base < 0 || length <= 0) {
                throw std::invalid_argument("rehydrate received an invalid memory block");
            }
        } else if (length == 0) {
            throw std::invalid_argument("rehydrate received an invalid memory block");
        }

        const auto lengthAsSize = static_cast<std::size_t>(length);
        if (mappedSize > std::numeric_limits<std::size_t>::max() - lengthAsSize) {
            throw std::overflow_error("rehydrate mapped size overflow");
        }
        mappedSize += lengthAsSize;

        const auto maxAddress = static_cast<std::uintmax_t>(std::numeric_limits<AddressType>::max());
        const auto baseAsWide = static_cast<std::uintmax_t>(base);
        const auto lengthAsWide = static_cast<std::uintmax_t>(length);
        if (lengthAsWide - 1u > maxAddress - baseAsWide) {
            throw std::invalid_argument("rehydrate memory block overflows address space");
        }
    }

    if (mappedSize != newMem.size()) {
        throw std::invalid_argument("rehydrate memory data does not match memory map");
    }

    mem = std::move(newMem);
    map = std::move(newMap);
}

} // namespace BMMQ
