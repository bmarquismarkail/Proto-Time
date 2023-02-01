#include "../MemoryStorage.hpp"

#include <limits>

namespace BMMQ {

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::addMemBlock(std::tuple<AddressType, AddressType, memAccess> memBlock)
{
    map.push_back(memBlock);
    mem.resize( std::get<1>(memBlock) );
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::addReadOnlyMem(std::pair<AddressType, AddressType> romBlock)
{
    auto memBlock = std::tuple_cat(romBlock, memAccess::MEM_READ);
    addMemBlock(memBlock);
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::addWriteOnlyMem(std::pair<AddressType, AddressType> womBlock)
{
    auto memBlock = std::tuple_cat(womBlock, memAccess::MEM_WRITE);
    addMemBlock(memBlock);
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::addReadWriteMem(std::pair<AddressType, AddressType> block)
{
    auto memBlock = std::tuple_cat(block, memAccess::MEM_READ_WRITE);
    addMemBlock(memBlock);
}
/*
template<typename AddressType, typename DataType>
DataType MemoryStorage<AddressType,DataType>::read(std::size_t address)
{
    std::size_t index;
    std::size_t temp = 0;

    for(auto i : map) {
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            index = temp + (address - std::get<0>(i));
            return mem[index];
        }

        temp+= std::get<1>(i);
    }

    return 0;
}
	*/
template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::read(DataType* stream, AddressType address, AddressType count){
	
	AddressType maxaddress = std::numeric_limits<AddressType>::max();
	AddressType bounds = maxaddress - address;
	count = std::min(bounds, count);
	if (count == 0) return;
	
	AddressType index = address;
	AddressType temp = 0;
	AddressType cap = 0;
	
	// Check if the addresses are readable
	for(auto i = map.begin(); i != map.end(); ++i) {
        if ( std::get<0>(*i) <= address && address < ( std::get<0>(*i) + std::get<1>(*i) ) ) {
			if ( std::get<2>(*i) & MEM_READ ) {
				index = temp + (address - std::get<0>(*i));
                auto next_it = std::next(i);
				cap = ( std::next(i) == map.end() ? mem.size() : std::get<1>(*next_it)) - index;
				break;
			}
			return;
        }
        temp+= std::get<1>(*i);
    }
	
	for(int i = 0; i < count; i--){
		*stream++ = mem[index + count];
	}
}

template<typename AddressType, typename DataType>
void MemoryStorage<AddressType,DataType>::write(DataType* stream, AddressType address, AddressType count )
{
    std::size_t index;
    std::size_t temp = 0;

    for(auto i : map) {
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            if ( std::get<2>(i) & MEM_WRITE )
                index = temp + (address - std::get<0>(i));
            else return;
        }

        temp+= std::get<1>(i);
    }

    char *ptr = (char*)stream;
    for (int c = 0; c < count; c++)
        mem[index + c] = *ptr++;
}

template<typename AddressType, typename DataType>
DataType* MemoryStorage<AddressType,DataType>::getPos(std::size_t address)
{
    std::size_t index;
    std::size_t temp = 0;

    for(auto i : map) {
        if ( std::get<0>(i) <= address && address < ( std::get<0>(i) + std::get<1>(i) ) ) {
            if ( std::get<2>(i) == MEM_READ ) {
                index = temp + (address - std::get<0>(i));
                return &(mem[index]);
            }
            else return nullptr;
        }

        temp+= std::get<1>(i);
    }
    return &(mem[0]);
}
}