
namespace BMMQ {
	
	template<typename regType>
		std::vector<IOpcode<regType>>& executionBlock<regType>::getBlock() { return code; }
		
	template<typename regType>
		const std::vector<IOpcode<regType>>& executionBlock<regType>::getBlock() const { return code; }	
	
	template<typename regType>
		CPU_Register<regType>& executionBlock<regType>::emplaceRegister(std::string_view id, bool isPtr){
			
			auto check = file.findRegister(id);
			if ( check != nullptr)
					return check;
			return (file.addRegister(id, isPtr) ).second; ;
			
		}
	
	template<typename regType>
	const RegisterFile<regType>& executionBlock<regType>::getRegisterFile() const {
		return file;
	}
}