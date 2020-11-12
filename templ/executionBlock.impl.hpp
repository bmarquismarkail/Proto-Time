// using executionBlock = std::vector<IOpcode>;

namespace BMMQ {
	
	template<typename regType>
		std::vector<IOpcode>& executionBlock<regType>::getBlock() { return code; }
		
	template<typename regType>
		const std::vector<IOpcode>& executionBlock<regType>::getBlock() const { return code; }	
	
	template<typename regType>
		CPU_Register<regType> executionBlock<regType>::emplaceRegister(std::string id, bool isPtr){
			
			auto check = file.findRegister(id);
			if ( check != nullptr)
					return check;
			file.addRegister(id, isPtr);
		}
}