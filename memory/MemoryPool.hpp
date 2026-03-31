#ifndef __MEMPOOL
#define __MEMPOOL

#include "IMemory.hpp"
#include "MemoryStorage.hpp"
#include "reg_base.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
struct MemoryPool : virtual public IMemory<AddressType, DataType, RegType>{
	virtual void read(std::span<DataType> stream, AddressType address);
	virtual void write(std::span<const DataType> stream, AddressType address);
	void read(DataType* stream, AddressType address, AddressType count)
	{
		read(std::span<DataType>(stream, count), address);
	}
	void write(const DataType* stream, AddressType address, AddressType count)
	{
		write(std::span<const DataType>(stream, count), address);
	}
    MemoryStorage<AddressType, DataType> store;
    RegisterFile<RegType> file;
};

}

#include "templ/MemoryPool.impl.hpp"
#endif // __MEMPOOL
