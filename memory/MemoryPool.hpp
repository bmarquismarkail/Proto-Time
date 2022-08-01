#ifndef __MEMPOOL
#define __MEMPOOL

#include "IMemory.hpp"
#include "MemoryStorage.hpp"
#include "reg_base.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
struct MemoryPool : virtual public IMemory<AddressType, DataType, RegType>{
	virtual void read(DataType* stream, AddressType address, AddressType count = 1);
	virtual void write(DataType* stream, AddressType address, AddressType count = 1);
    MemoryStorage<AddressType, DataType> map;
    RegisterFile<RegType> file;
};

}

#include "templ/MemoryPool.impl.hpp"
#endif // __MEMPOOL