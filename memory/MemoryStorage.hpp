#ifndef MEM_MAP
#define MEM_MAP

#include<ios>
#include<vector>
#include<tuple>

namespace BMMQ {

enum memAccess {
    MEM_UNMAPPED,
    MEM_READ 		= 1,
    MEM_WRITE		= 2,
    MEM_READ_WRITE	= 3
};

// The Memory Map
template<typename AddressType, typename DataType>
class MemoryStorage {
public:
    using starting_address_t = AddressType;
    using ending_address_t = AddressType;
    void addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock);
    void addReadOnlyMem(std::pair<AddressType, AddressType> romBlock);
    void addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock);
    void addReadWriteMem(std::pair<AddressType, AddressType> block);
    //DataType read(std::size_t address);
	void read(DataType* stream, AddressType address, AddressType count = 1);
    void write(DataType *value, AddressType address, AddressType count = 1);
    DataType *getPos(std::size_t address);
private:
    std::vector<std::tuple<starting_address_t, ending_address_t, memAccess>> map;
    std::vector<DataType> mem;
};
//////////////////////////////////////////////////////////
}
#include "templ/MemoryStorage.impl.hpp"
#endif
