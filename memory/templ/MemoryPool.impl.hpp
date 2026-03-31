namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
void MemoryPool<AddressType, DataType, RegType>::read(std::span<DataType> stream, AddressType address)
{
	store.read(stream, address);
}

template<typename AddressType, typename DataType, typename RegType>
void MemoryPool<AddressType, DataType, RegType>::write(std::span<const DataType> stream, AddressType address)
{
	store.write(stream, address);
}

} // namespace BMMQ
