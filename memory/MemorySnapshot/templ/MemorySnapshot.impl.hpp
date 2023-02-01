#include <algorithm>
#include <cstdlib>

namespace BMMQ {

	template<typename AddressType, typename DataType, typename RegType>
		MemorySnapshot<AddressType, DataType, RegType>::MemorySnapshot(MemoryStorage<AddressType, DataType>& m)
		: mem(m){}

	template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::read(DataType* stream, AddressType address, AddressType count){
			mem.read(stream, address, count);
		}

	template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::write(DataType* stream, AddressType address, AddressType count){
			mem.write(stream, address, count);
		}

  template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::copyRegisterFromMainFile
			(std::string_view regId, RegisterFile<RegType>& from){
			file.findOrInsert(regId)->second = from.findRegister(regId)->second;
		}

	template<typename AddressType, typename DataType, typename RegType>
		CPU_Register<RegType>* MemorySnapshot<AddressType, DataType, RegType>::findOrCreateNewRegister
			(const std::string& regId, bool isPair){
			return file.findOrInsert(regId,isPair)->second;
		}
}
