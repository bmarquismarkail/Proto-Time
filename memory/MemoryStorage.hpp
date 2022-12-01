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
    void addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock);
    void addReadOnlyMem(std::pair<AddressType, AddressType> romBlock);
    void addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock);
    void addReadWriteMem(std::pair<AddressType, AddressType> block);
    //DataType read(std::size_t address);
	void read(DataType* stream, std::size_t address, std::streamsize count = 1);
    void write(std::size_t address, void *value, std::streamsize count = 1);
    DataType *getPos(std::size_t address);
private:
    std::vector<std::tuple<AddressType, AddressType, memAccess>> map;
    std::vector<DataType> mem;
};
//////////////////////////////////////////////////////////
}
#include "templ/MemoryStorage.impl.hpp"
#endif
