#ifndef MEM_MAP
#define MEM_MAP

#include "MemoryPool.hpp"
#include "templ/reg_base.hpp"
namespace BMMQ {
	
	template<typename AddressType, typename DataType, typename RegType>
	struct MemoryMap {
		MemoryPool<AddressType, DataType> pool;
		RegisterFile<RegType> file;
	};
}
#endif