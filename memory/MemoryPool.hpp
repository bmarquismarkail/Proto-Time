#ifndef __MEMPOOL
#define __MEMPOOL

#include <utility>

#include "IMemory.hpp"
#include "MemoryStorage.hpp"
#include "reg_base.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
struct MemoryPool : virtual public IMemory<AddressType, DataType, RegType>{
	virtual void read(std::span<DataType> stream, AddressType address);
	virtual void write(std::span<const DataType> stream, AddressType address);
	MemoryPool() = default;
	MemoryPool(const MemoryPool& other) : store(other.store), activeStore(&store), file(other.file)
	{
		if (other.activeStore != &other.store) {
			activeStore = other.activeStore;
		}
	}
	MemoryPool& operator=(const MemoryPool& other)
	{
		if (this != &other) {
			store = other.store;
			file = other.file;
			activeStore = &store;
			if (other.activeStore != &other.store) {
				activeStore = other.activeStore;
			}
		}
		return *this;
	}
	MemoryPool(MemoryPool&& other) noexcept : store(std::move(other.store)), activeStore(&store), file(std::move(other.file))
	{
		if (other.activeStore != &other.store) {
			activeStore = other.activeStore;
		}
	}
	MemoryPool& operator=(MemoryPool&& other) noexcept
	{
		if (this != &other) {
			store = std::move(other.store);
			file = std::move(other.file);
			activeStore = &store;
			if (other.activeStore != &other.store) {
				activeStore = other.activeStore;
			}
		}
		return *this;
	}
	void attachStore(MemoryStorage<AddressType, DataType>& externalStore)
	{
		activeStore = &externalStore;
	}
	void detachStore()
	{
		activeStore = &store;
	}
	[[nodiscard]] MemoryStorage<AddressType, DataType>& backingStore()
	{
		return *activeStore;
	}
	[[nodiscard]] const MemoryStorage<AddressType, DataType>& backingStore() const
	{
		return *activeStore;
	}
	void read(DataType* stream, AddressType address, AddressType count)
	{
		read(std::span<DataType>(stream, count), address);
	}
	void write(const DataType* stream, AddressType address, AddressType count)
	{
		write(std::span<const DataType>(stream, count), address);
	}
    MemoryStorage<AddressType, DataType> store;
    MemoryStorage<AddressType, DataType>* activeStore = &store;
    RegisterFile<RegType> file;
};

}

#include "templ/MemoryPool.impl.hpp"
#endif // __MEMPOOL
