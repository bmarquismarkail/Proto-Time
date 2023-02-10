#ifndef OPCODE_H
#define OPCODE_H

// Opcodes are technically tuples of tuples of functions
// the purpose of the Opcode class is to encapsulate every single microcode needed to run a single operation
#include <utility>

#include "microcode.hpp"

namespace BMMQ
{
    template <is_microcode... MCs>
    struct Opcode
    {
        std::tuple<MCs...> microcodeList;
        Opcode(MCs... mcs) : microcodeList(std::make_tuple(mcs...)){
        }

        // The default function call constructor is intentially deleted
        virtual void operator()() = delete;
    };

    // helper code to create opcodes from microcodes
    template <is_microcode... MCs>
    auto make_opcode(MCs... mcs)
    {
        return Opcode<MCs...>(mcs...);
    }

    //concept to check if class is an Opcode
    template <typename T>
    concept is_opcode = requires{
        T::microcodeList;
    };


    // An opcodeList is a tuple of opcodes
    template<is_opcode... opcs>
    using opcodeList = std::tuple<opcs...>;
}
#endif