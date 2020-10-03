#ifndef INSTRUCTION_CYCLE
#define INSTRUCTION_CYCLE

#include "templ/reg_base.hpp"
#include "memory_pool.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
struct fetchBlockData {
    AddressType offset;
    std::vector<DataType> data;
};

template<typename AddressType, typename DataType>
class fetchBlock {
    AddressType baseAddress;
    memoryStorage<AddressType, DataType> store;
    RegisterFile<AddressType> baseRegister, file;
    std::vector<fetchBlockData<AddressType, DataType>> blockData;

public:
// Public Methods
    void setbaseAddress(AddressType address)
    {
        baseAddress = address;
    }
    AddressType getbaseAddress() const
    {
        return baseAddress;
    }
    std::vector<fetchBlockData<AddressType, DataType>> &getblockData()
    {
        return blockData;
    }
    void setRegisterFile(RegisterFile<AddressType> rf)
    {
        baseRegister = rf;
    }
    CPU_Register<AddressType> *getRegisterAt(const std::string id, AddressType offset, RegisterFile<AddressType> mainFile )
    {
        if (file.hasRegister(id) )
            return file.findRegister(id)->second;
        else return mainFile.findRegister(id)->second;
    }
};
}

#endif // INSTRUCTION_CYCLE