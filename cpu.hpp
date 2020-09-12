#ifndef BMMQ_CPU
#define BMMQ_CPU

#include "opcode.hpp"
#include "inst_cycle.hpp"

namespace BMMQ {
///////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////


template<typename T>
struct _register {
    virtual T operator()() = 0;
};

template<typename T>
struct CPU_Register :  public _register<T> {
    T value;
	T operator()(){return value;}
};

template<typename T>
struct CPU_RegisterPair :  public _register<T> {
    union {
        struct {
			T lo: sizeof(T) * 4;
			T hi: sizeof(T) * 4;
        };
        T value;
    };
	
	T operator()(){return value;}
};

template<typename AddressType, typename DataType>
class CPU {
public:
    virtual fetchBlock<AddressType, DataType> fetch()=0;
    virtual executionBlock decode(OpcodeList<DataType> &oplist, const fetchBlock<AddressType, DataType>& fetchData)=0;
    virtual void execute(const executionBlock& block)=0;
};
}

#endif //BMMQ_CPU