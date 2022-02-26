#ifndef MEMORY_SNAPSHOT_H
#define MEMORY_SNAPSHOT_H

#include <utility>
#include <vector>
#include <string_view>
#include "reg_base.hpp"
#include "MemoryMap.hpp"
#include "SnapshotStorage/SnapshotStorage.h"

namespace BMMQ {
	
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