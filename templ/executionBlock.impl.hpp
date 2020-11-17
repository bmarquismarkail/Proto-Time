
namespace BMMQ {
	
template<typename AddressType, typename DataType, typename RegType>
	std::vector<IOpcode<AddressType, DataType, RegType>>& executionBlock<AddressType, DataType, RegType>::getBlock() { return code; }
	
template<typename AddressType, typename DataType, typename RegType>
	const std::vector<IOpcode<AddressType, DataType, RegType>>& executionBlock<AddressType, DataType, RegType>::getBlock() const { return code; }

template<typename AddressType, typename DataType, typename RegType>
	CPU_Register<RegType>& executionBlock<AddressType, DataType, RegType>::emplaceRegister(std::string_view id, bool isPtr){
		
		auto check = mem.file.findRegister(id);
		if ( check != nullptr)
				return check;
		return (mem.file.addRegister(id, isPtr) ).second; ;
		
	}

template<typename AddressType, typename DataType, typename RegType>
const MemoryMap<AddressType, DataType, RegType>& executionBlock<AddressType, DataType, RegType>::getMemory() const {
	return mem;
}
}