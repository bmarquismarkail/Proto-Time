#include <algorithm>
#include <cstdlib>

namespace BMMQ {

  template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::copyRegisterFromMainFile
			(std::string_view regId, RegisterFile<AddressType>& from){
			*(file.findOrInsert(regId)).value = *(from.findRegister(regId)).value;
		}

	template<typename AddressType, typename DataType, typename RegType>
		CPU_Register<RegType>* MemorySnapshot<AddressType, DataType, RegType>::findOrCreateNewRegister
			(const std::string& regId, bool isPair){
			return file.findOrInsert(regId,isPair)->second;
		}
}
