#ifndef __MEMPOOL
#define __MEMPOOL

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

// The Memory
// A pool of memory
template<typename AddressType, typename DataType>
class MemoryPool {
public:
    void setMemBlock(AddressType blockStart, AddressType blockLength, memAccess memType);
    DataType read(std::size_t address);
    DataType *getPos(std::size_t address);
    void write(std::size_t address, void *value, std::streamsize count = 1);
private:
    std::vector<std::tuple<AddressType, AddressType, memAccess>> memoryMap;
    std::vector<DataType> mem;
};
//////////////////////////////////////////////////////////

template<typename AddressType, typename DataType>
using memoryStorage =  std::pair<AddressType, DataType*>;
}

#include "templ/MemoryPool.impl.hpp"
#endif // __MEMPOOL