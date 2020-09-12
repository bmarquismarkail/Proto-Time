#ifndef INSTRUCTION_CYCLE
#define INSTRUCTION_CYCLE

#include "opcode.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
struct fetchBlockData {
    AddressType offset;
    std::vector<DataType> data;
};

template<typename AddressType, typename DataType>
struct fetchBlock {
    AddressType baseAddress;
    std::vector<fetchBlockData<AddressType, DataType>> blockData;
};

template<typename DataType>
using OpcodeList = std::vector<IOpcode>;

using executionBlock = std::vector<IOpcode>;
}

#endif // INSTRUCTION_CYCLE