
namespace BMMQ {
template<typename AddressType, typename DataType, typename RegType>
	void MemoryPool<AddressType, DataType, RegType>::read(DataType* stream, AddressType address, AddressType count){
		map.read(stream, address,count);
	}

	template<typename AddressType, typename DataType, typename RegType>
	void MemoryPool<AddressType, DataType, RegType>::write(DataType* stream, AddressType address, AddressType count){
	map.write(stream, address, count);
	}
}
