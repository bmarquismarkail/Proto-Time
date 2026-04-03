#ifndef MEMORY_SNAPSHOT_H
#define MEMORY_SNAPSHOT_H

#include <utility>
#include <vector>
#include <span>
#include <string_view>
#include "IMemory.hpp"
#include "reg_base.hpp"
#include "SnapshotStorage/SnapshotStorage.h"

namespace BMMQ {
	
	template<typename AddressType, typename DataType, typename RegType>
	struct MemorySnapshot : virtual public IMemory<AddressType, DataType, RegType> {
		MemorySnapshot(MemoryStorage<AddressType, DataType>& m);
		void read(std::span<DataType> stream, AddressType address) override;
		void write(std::span<const DataType> stream, AddressType address) override;
		void copyRegisterFromMainFile(std::string_view regId, RegisterFile<RegType>& from);
		CPU_Register<RegType>* findOrCreateNewRegister(const std::string& regId, bool isPair= false);
		RegisterFile<RegType> file;
		SnapshotStorage<AddressType, DataType> mem;
	};
}

#include "templ/MemorySnapshot.impl.hpp"
#endif
