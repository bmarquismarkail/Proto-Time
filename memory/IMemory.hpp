#ifndef __IMEMORY
#define __IMEMORY

#include <span>

namespace BMMQ {

	template<typename AddressType, typename DataType, typename RegType>
	class IMemory {
	public:
		virtual ~IMemory() = default;
		virtual void read(std::span<DataType> stream, AddressType address) = 0;
		virtual void write(std::span<const DataType> stream, AddressType address) = 0;
	};
}

#endif
