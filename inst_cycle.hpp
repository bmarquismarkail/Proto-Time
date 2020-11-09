#ifndef INSTRUCTION_CYCLE
#define INSTRUCTION_CYCLE

#include <vector>
#include <string>

#include "templ/reg_base.hpp"
#include "MemoryPool.hpp"

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
    void setbaseAddress(AddressType address);
    AddressType getbaseAddress() const;
    std::vector<fetchBlockData<AddressType, DataType>> &getblockData();
    void setRegisterFile(RegisterFile<AddressType> rf);
    CPU_Register<AddressType> *getRegisterAt(const std::string id, AddressType offset, RegisterFile<AddressType> mainFile );
};
}

#include "inst_cycle.impl.hpp"
#endif // INSTRUCTION_CYCLE