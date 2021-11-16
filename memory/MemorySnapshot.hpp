#ifndef MEMORY_SNAPSHOT_H
#define MEMORY_SNAPSHOT_H

#include <utility>
#include <vector>
#include <string_view>
#include "reg_base.hpp"
#include "MemoryMap.hpp"

namespace BMMQ {
	
	template<typename AddressType, typename DataType>
	struct addressReturnData{
		bool isAddressInSnapshot;
		std::pair< AddressType, std::size_t>* nearest_entry;
		bool contiguous;
		addressReturnData(bool retFlag, std::pair< AddressType, std::size_t>* entry, bool contigFlag);
	};
	
	template<typename AddressType, typename DataType>
	class SnapshotStorage {
		std::vector< std::pair< AddressType, std::size_t>> pool;
		std::vector<DataType> mem;
		addressReturnData<AddressType, DataType> isAddressInSnapshot(AddressType at);
	public:
		void read(DataType* stream, AddressType address, std::size_t count, MemoryMap<AddressType, DataType>& main);
		void write(DataType* stream, AddressType address, std::size_t count);
	};
	
	template<typename AddressType, typename DataType, typename RegType>
	struct MemorySnapshot {
		void copyRegisterFromMainFile(std::string_view regId, RegisterFile<AddressType>& from);
		CPU_Register<RegType>* findOrCreateNewRegister(const std::string& regId, bool isPair= false);
		RegisterFile<RegType> file;
		SnapshotStorage<AddressType, DataType> mem;
	};
}

#include "templ/MemorySnapshot.impl.hpp"
#endif