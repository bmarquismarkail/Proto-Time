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
	RegisterFile<AddressType> baseRegister;
    std::vector<fetchBlockData<AddressType, DataType>> blockData;

public:
// Public Methods
	void setbaseAddress(AddressType address){baseAddress = address;}
	AddressType getbaseAddress() const {return baseAddress;} 
	std::vector<fetchBlockData<AddressType, DataType>> &getblockData() {return blockData;}
};
}

#endif // INSTRUCTION_CYCLE