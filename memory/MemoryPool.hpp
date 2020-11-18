#ifndef __MEMPOOL
#define __MEMPOOL

#include "MemoryMap.hpp"
#include "reg_base.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
	struct MemoryPool {
		MemoryMap<AddressType, DataType> map;
		RegisterFile<RegType> file;
	};

}
#endif // __MEMPOOL