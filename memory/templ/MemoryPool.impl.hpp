
namespace BMMQ {
template<typename AddressType, typename DataType, typename RegType>
	void MemoryPool<AddressType, DataType, RegType>::read(DataType* stream, AddressType address, AddressType count){
		store.read(stream, address,count);
	}

	template<typename AddressType, typename DataType, typename RegType>
	void MemoryPool<AddressType, DataType, RegType>::write(DataType* stream, AddressType address, AddressType count){
	store.write(stream, address, count);
	}
}
