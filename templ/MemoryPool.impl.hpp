#include "../MemoryPool.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::setMemBlock(AddressType blockStart, AddressType blockLength, memAccess memType)
{
    memoryMap.push_back(std::make_tuple(blockStart, blockLength, memType));
    mem.resize(blockLength);
};

template<typename AddressType, typename DataType>
DataType MemoryPool<AddressType,DataType>::read(std::size_t address)
{
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

template<typename AddressType, typename DataType>
DataType* MemoryPool<AddressType,DataType>::getPos(std::size_t address)
{
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

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::write(std::size_t address, void *value, std::streamsize count )
{
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
}