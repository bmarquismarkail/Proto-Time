#include <algorithm>
#include <cstdlib>

namespace BMMQ {

	template<typename AddressType, typename DataType, typename RegType>
		MemorySnapshot<AddressType, DataType, RegType>::MemorySnapshot(MemoryStorage<AddressType, DataType>& m)
		: mem(m){}

	template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::read(std::span<DataType> stream, AddressType address){
			mem.read(stream, address);
		}

	template<typename AddressType, typename DataType, typename RegType>
		void MemorySnapshot<AddressType, DataType, RegType>::write(std::span<const DataType> stream, AddressType address){
			mem.write(stream, address);
		}

  template<typename AddressType, typename DataType, typename RegType>
			void MemorySnapshot<AddressType, DataType, RegType>::copyRegisterFromMainFile
				(std::string_view regId, RegisterFile<RegType>& from){
				auto from_entry = from.findRegister(regId);
				if (from_entry == nullptr) return;
				auto isPair = dynamic_cast<CPU_RegisterPair<RegType>*>(from_entry->reg.get()) != nullptr;
				auto& to_entry = file.findOrInsert(RegisterDescriptor{
					std::string(regId),
					from_entry->descriptor.width,
					RegisterStorage::RegisterFile,
					std::nullopt,
					isPair
				});
				to_entry.reg->value = from_entry->reg->value;
			}

	template<typename AddressType, typename DataType, typename RegType>
		CPU_Register<RegType>* MemorySnapshot<AddressType, DataType, RegType>::findOrCreateNewRegister
			(const std::string& regId, bool isPair){
			return file.findOrInsert(regId,isPair).reg.get();
		}
}
