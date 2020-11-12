#include <vector>
#include <string>
#include "reg_base.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
void fetchBlock<AddressType, DataType>::setbaseAddress(AddressType address)
{
    baseAddress = address;
}

template<typename AddressType, typename DataType>
AddressType fetchBlock<AddressType, DataType>::getbaseAddress() const
{
    return baseAddress;
}

template<typename AddressType, typename DataType>
std::vector<fetchBlockData<AddressType, DataType>>& fetchBlock<AddressType, DataType>::getblockData()
{
    return blockData;
}
}