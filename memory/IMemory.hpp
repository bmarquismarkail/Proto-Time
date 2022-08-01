#ifndef __IMEMORY
#define __IMEMORY

namespace BMMQ {	
	template<typename AddressType, typename DataType, typename RegType>
	class IMemory{
		virtual void read(DataType* stream, AddressType address, AddressType count) = 0;
		virtual void write(DataType* stream, AddressType address, AddressType count) = 0;
	};
}

#endif