#include <vector>
#include <string>
#include "templ/reg_base.hpp"

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

template<typename AddressType, typename DataType>
void fetchBlock<AddressType, DataType>::setRegisterFile(RegisterFile<AddressType> rf)
{
    baseRegister = rf;
}

template<typename AddressType, typename DataType>
CPU_Register<AddressType>* fetchBlock<AddressType, DataType>::getRegisterAt(const std::string id, AddressType offset, RegisterFile<AddressType> mainFile )
{
    if (file.hasRegister(id) )
        return file.findRegister(id)->second;
    else return mainFile.findRegister(id)->second;
}
}