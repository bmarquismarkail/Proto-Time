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
class MemoryMap {
public:
	void addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock);
	void addReadOnlyMem(std::pair<AddressType, AddressType> romBlock);
	void addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock);
	void addReadWriteMem(std::pair<AddressType, AddressType> block);
    DataType read(std::size_t address);
    DataType *getPos(std::size_t address);
    void write(std::size_t address, void *value, std::streamsize count = 1);
	void setMem(std::vector<DataType> *m);
private:
    std::vector<std::tuple<AddressType, AddressType, memAccess>> map;
    std::vector<DataType> *mem;
};
//////////////////////////////////////////////////////////
template<typename AddressType, typename DataType>
using memoryStorage =  std::pair<AddressType, DataType*>;
}

#include "templ/MemoryMap.impl.hpp"
#endif