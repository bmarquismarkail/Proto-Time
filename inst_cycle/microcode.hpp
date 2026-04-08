#ifndef microcode_h
#define microcode_h

#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>

#include "execute/executionBlock.hpp"
#include "fetch/fetchBlock.hpp"

namespace BMMQ {

template <typename AddressType, typename DataType, typename RegType>
class microcode {
public:
    using block_t = executionBlock<AddressType, DataType, RegType>;
    using fetch_data_t = fetchBlockData<AddressType, DataType>;
    using emit_t = std::function<void(block_t&, const fetch_data_t&, std::size_t)>;

    microcode() = default;

    explicit microcode(emit_t emit) : emit_(std::move(emit)) {}

    void emit(block_t& block, const fetch_data_t& fetchData, std::size_t opcodeIndex) const {
        assert(static_cast<bool>(emit_));
        emit_(block, fetchData, opcodeIndex);
    }

    explicit operator bool() const {
        return static_cast<bool>(emit_);
    }

private:
    emit_t emit_;
};

template <typename AddressType, typename DataType, typename RegType, typename F>
auto make_microcode(F&& emit) {
    using microcode_t = microcode<AddressType, DataType, RegType>;
    return microcode_t(typename microcode_t::emit_t(std::forward<F>(emit)));
}

template <typename T>
concept is_microcode = requires(
    const T& micro,
    typename T::block_t& block,
    const typename T::fetch_data_t& fetchData,
    std::size_t opcodeIndex) {
    micro.emit(block, fetchData, opcodeIndex);
};
}
#endif
