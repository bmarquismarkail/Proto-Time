#ifndef OPCODE_H
#define OPCODE_H

// Opcodes are containers of decode-time microcode emission steps.
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "microcode.hpp"

namespace BMMQ
{
    struct OpcodeCycles
    {
        std::uint8_t notTaken = 0;
        std::uint8_t taken = 0;

        constexpr OpcodeCycles() = default;
        constexpr explicit OpcodeCycles(std::uint8_t cycles)
            : notTaken(cycles), taken(cycles) {
        }
        constexpr OpcodeCycles(std::uint8_t notTakenCycles, std::uint8_t takenCycles)
            : notTaken(notTakenCycles), taken(takenCycles) {
        }
    };

    template <typename AddressType, typename DataType, typename RegType>
    class Opcode
    {
    public:
        using microcode_t = microcode<AddressType, DataType, RegType>;
        using block_t = executionBlock<AddressType, DataType, RegType>;
        using fetch_data_t = fetchBlockData<AddressType, DataType>;
        using cycle_resolver_t = std::function<OpcodeCycles(const fetch_data_t&, std::size_t)>;

        Opcode() = default;

        Opcode(std::uint8_t length,
               cycle_resolver_t cycleResolver,
               std::vector<microcode_t> microcodeList)
            : length_(length),
              cycleResolver_(std::move(cycleResolver)),
              microcodeList_(std::move(microcodeList)) {
        }

        std::uint8_t length() const {
            return length_;
        }

        void emit(block_t& block, const fetch_data_t& fetchData, std::size_t opcodeIndex) const {
            for (const auto& micro : microcodeList_) {
                micro.emit(block, fetchData, opcodeIndex);
            }
        }

        OpcodeCycles cycles(const fetch_data_t& fetchData, std::size_t opcodeIndex) const {
            if (!cycleResolver_) {
                return OpcodeCycles{};
            }
            return cycleResolver_(fetchData, opcodeIndex);
        }

        // Treats the opcode as empty only when it carries no microcodes and no encoded length.
        bool empty() const {
            return microcodeList_.empty() && length_ == 0;
        }

    private:
        std::uint8_t length_ = 0;
        cycle_resolver_t cycleResolver_{};
        std::vector<microcode_t> microcodeList_;
    };

    template <typename AddressType, typename DataType, typename RegType, is_microcode... MCs>
    auto make_opcode(std::uint8_t length, OpcodeCycles cycles, MCs&&... mcs)
    {
        using opcode_t = Opcode<AddressType, DataType, RegType>;
        using microcode_t = typename opcode_t::microcode_t;
        std::vector<microcode_t> microcodes;
        microcodes.reserve(sizeof...(mcs));
        (microcodes.push_back(std::forward<MCs>(mcs)), ...);
        return opcode_t(length,
                        [cycles](const typename opcode_t::fetch_data_t&, std::size_t) {
                            return cycles;
                        },
                        std::move(microcodes));
    }

    template <typename AddressType, typename DataType, typename RegType, typename CycleResolver, is_microcode... MCs>
    auto make_opcode(std::uint8_t length, CycleResolver&& cycleResolver, MCs&&... mcs)
    {
        using opcode_t = Opcode<AddressType, DataType, RegType>;
        using microcode_t = typename opcode_t::microcode_t;
        std::vector<microcode_t> microcodes;
        microcodes.reserve(sizeof...(mcs));
        (microcodes.push_back(std::forward<MCs>(mcs)), ...);
        return opcode_t(length, std::forward<CycleResolver>(cycleResolver), std::move(microcodes));
    }

    template <typename T>
    concept is_opcode = requires(
        const T& opcode,
        typename T::block_t& block,
        const typename T::fetch_data_t& fetchData,
        std::size_t opcodeIndex) {
        { opcode.length() } -> std::convertible_to<std::uint8_t>;
        { opcode.cycles(fetchData, opcodeIndex) } -> std::same_as<OpcodeCycles>;
        opcode.emit(block, fetchData, opcodeIndex);
    };
}
#endif
