#ifndef INSTRUCTION_CYCLE
#define INSTRUCTION_CYCLE

#include <vector>
#include <string>

namespace BMMQ {

template<typename AddressType, typename DataType>
struct fetchBlockData {
    AddressType offset;
    std::vector<DataType> data;
};

template<typename AddressType, typename DataType>
class fetchBlock {
    AddressType baseAddress;
    std::vector<fetchBlockData<AddressType, DataType>> blockData;

public:
    void setbaseAddress(AddressType address);
    AddressType getbaseAddress() const;
    std::vector<fetchBlockData<AddressType, DataType>> &getblockData();
};
}

#include "templ/inst_cycle.impl.hpp"
#endif // INSTRUCTION_CYCLE