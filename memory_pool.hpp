#ifndef __MEMPOOL
#define __MEMPOOL

namespace BMMQ {

enum memAccess {
    MEM_READ 		= 1,
    MEM_WRITE		= 2,
    MEM_READ_WRITE	= 3
};

// The Memory
// A pool of memory
template<typename AddressType, typename DataType>
class MemoryPool {
public:
    void setMemBlock(AddressType blockStart, AddressType blockLength, memAccess memType){
    memoryMap.push_back(std::make_tuple(blockStart, blockLength, memType));
    mem.resize(blockLength);
};
 
 DataType read(std::size_t address){
	std::size_t index;
    std::size_t temp = 0;

    for(auto i : memoryMap) {
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            index = temp + (address - std::get<0>(i));
			return mem[index];
        }

        temp+= std::get<1>(i);
    }

    return 0;
}

 DataType *getPos(std::size_t address){
	std::size_t index;
    std::size_t temp = 0;

    for(auto i : memoryMap) {
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            index = temp + (address - std::get<0>(i));
			return &mem[index];
        }

        temp+= std::get<1>(i);
    }

    return &mem[0];
}
 
 void write(std::size_t address, void *value, std::streamsize count = 1 ){
    std::size_t index;
    std::size_t temp = 0;

    for(auto i : memoryMap) {
        if ( 	std::get<0>(i) <= address
                && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            if ( std::get<2>(i) | MEM_WRITE )
                index = temp + (address - std::get<0>(i));
        }

        temp+= std::get<1>(i);
    }

    char *ptr = (char*)value;
    for (int c = 0; c < count; c++)
        mem[index + c] = *ptr++;
}

private:
    std::vector<std::tuple<AddressType, AddressType, memAccess>> memoryMap;
    std::vector<DataType> mem;
};
}
#endif // __MEMPOOL