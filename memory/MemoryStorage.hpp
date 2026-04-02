#ifndef MEM_MAP
#define MEM_MAP

#include <cassert>
#include <cstddef>
#include <functional>
#include <ios>
#include <span>
#include <tuple>
#include <type_traits>
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

// The Memory Map
template<typename AddressType, typename DataType>
class MemoryStorage {
public:
    using starting_address_t = AddressType;
    using ending_address_t = AddressType;
    void addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock);
    void addReadOnlyMem(std::pair<AddressType, AddressType> romBlock);
    void addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock);
    void addReadWriteMem(std::pair<AddressType, AddressType> block);
    void read(std::span<DataType> stream, AddressType address) const;
    void write(std::span<const DataType> value, AddressType address);
    void load(std::span<const DataType> value, AddressType address);
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
private:
    std::vector<std::tuple<starting_address_t, ending_address_t, memAccess>> map;
    std::vector<DataType> mem;
    std::function<bool(AddressType, std::span<const DataType>)> writeInterceptor_;
    std::function<AddressType(AddressType)> addressTranslator_;
};
//////////////////////////////////////////////////////////
}
#include "templ/MemoryStorage.impl.hpp"
#endif
