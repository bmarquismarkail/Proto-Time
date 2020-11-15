#include "../MemoryPool.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock){
	memoryMap.push_back(memBlock);
	mem.resize( std::get<1>(memBlock) );
}

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::addReadOnlyMem(std::pair<AddressType, AddressType> romBlock){
	auto memBlock = std::tuple_cat(romBlock, memAccess::MEM_READ);
	addMemBlock(memBlock);
}

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock){
	auto memBlock = std::tuple_cat(womBlock, memAccess::MEM_WRITE);
	addMemBlock(memBlock);	
}

template<typename AddressType, typename DataType>
void MemoryPool<AddressType,DataType>::addMem(std::pair<AddressType, AddressType> block){	
	auto memBlock = std::tuple_cat(block, memAccess::MEM_READ_WRITE);
	addMemBlock(memBlock);	
}

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
            if ( std::get<2>(i) == MEM_READ ) {
				index = temp + (address - std::get<0>(i));
				return &mem[index];
			}
			else return nullptr;
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
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            if ( std::get<2>(i) | MEM_WRITE )
                index = temp + (address - std::get<0>(i));
			else return;
        }

        temp+= std::get<1>(i);
    }

    char *ptr = (char*)value;
    for (int c = 0; c < count; c++)
        mem[index + c] = *ptr++;
}
}