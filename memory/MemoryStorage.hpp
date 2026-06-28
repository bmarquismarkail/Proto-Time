#ifndef MEM_MAP
#define MEM_MAP

#include <cassert>
#include <cstddef>
#include <functional>
#include <ios>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace BMMQ {

enum class memAccess {
    Unmapped = 0,
    Read = 1,
    Write = 2,
    ReadWrite = 3,
};

constexpr bool hasAccess(memAccess value, memAccess flag) noexcept
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0;
}

constexpr bool isValidMemoryAccess(memAccess value) noexcept
{
    switch (value) {
    case memAccess::Unmapped:
    case memAccess::Read:
    case memAccess::Write:
    case memAccess::ReadWrite:
        return true;
    }
    return false;
}

// For rehydration: Unmapped blocks must be rejected; only Read/Write/ReadWrite are valid.
constexpr bool isValidRehydrateAccess(memAccess value) noexcept
{
    switch (value) {
    case memAccess::Read:
    case memAccess::Write:
    case memAccess::ReadWrite:
        return true;
    default:
        return false;
    }
}

// The Memory Map
template<typename AddressType, typename DataType>
class MemoryStorage {
public:
    using starting_address_t = AddressType;
    using ending_address_t = AddressType;
    virtual ~MemoryStorage() = default;
    void addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock);
    void addReadOnlyMem(std::pair<AddressType, AddressType> romBlock);
    void addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock);
    void addReadWriteMem(std::pair<AddressType, AddressType> block);
    virtual void read(std::span<DataType> stream, AddressType address) const;
    virtual void write(std::span<const DataType> value, AddressType address);
    void load(std::span<const DataType> value, AddressType address);
    void setReadInterceptor(std::function<bool(AddressType, std::span<DataType>)> interceptor)
    {
        readInterceptor_ = std::move(interceptor);
    }
    void setWriteInterceptor(std::function<bool(AddressType, std::span<const DataType>)> interceptor)
    {
        writeInterceptor_ = std::move(interceptor);
    }
    void setAddressTranslator(std::function<AddressType(AddressType)> translator)
    {
        addressTranslator_ = std::move(translator);
    }
    void read(DataType* stream, AddressType address, AddressType count) const
    {
        if constexpr (std::is_signed_v<AddressType>) {
            assert(count >= 0 && "count must be non-negative");
        }
        const auto spanCount = static_cast<std::size_t>(count);
        read(std::span<DataType>(stream, spanCount), address);
    }
    void write(const DataType* value, AddressType address, AddressType count)
    {
        if constexpr (std::is_signed_v<AddressType>) {
            assert(count >= 0 && "count must be non-negative");
        }
        const auto spanCount = static_cast<std::size_t>(count);
        write(std::span<const DataType>(value, spanCount), address);
    }
    [[nodiscard]] std::span<DataType> writableSpan(AddressType address, std::size_t count);
    [[nodiscard]] std::span<const DataType> readableSpan(AddressType address, std::size_t count) const;
    [[nodiscard]] memAccess accessAt(AddressType address) const;
    void rehydrate(std::vector<DataType> newMem, std::vector<std::tuple<AddressType, AddressType, memAccess>> newMap);
private:
    std::vector<std::tuple<starting_address_t, ending_address_t, memAccess>> map;
    std::vector<DataType> mem;
    std::function<bool(AddressType, std::span<DataType>)> readInterceptor_;
    std::function<bool(AddressType, std::span<const DataType>)> writeInterceptor_;
    std::function<AddressType(AddressType)> addressTranslator_;
};

}
#include "templ/MemoryStorage.impl.hpp"
#endif
