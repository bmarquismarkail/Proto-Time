#include <algorithm>
#include <cstdlib>

namespace BMMQ {
	
  template<typename AddressType, typename DataType>
	addressReturnData<AddressType, DataType>::addressReturnData(bool retFlag, std::pair< AddressType, std::size_t>* entry, bool contigFlag)
		: isAddressInSnapshot(retFlag), nearest_entry(entry), contiguous(contigFlag){}
	
	template<typename AddressType, typename DataType>
	addressReturnData<AddressType, DataType> SnapshotStorage<AddressType, DataType>::isAddressInSnapshot
		(AddressType at){
		if( pool.empty())
			return addressReturnData(false, nullptr, false);
		// find the pair closest to but not past at
		// the read and write functions will sort if necessary
		// so all we need to do is check adjacent elements 
		auto iter_start = pool.begin();
		for(; std::next(iter_start) != pool.end(); ++iter_start){
			if (iter_start->first > at)
				return addressReturnData(false, nullptr, false);
			if( std::abs( iter_start->first - std::next(iter_start)->first ) >= std::abs(iter_start-> first - at) )
				break;
		}
		auto target_offset = at - iter_start->first;
		auto entry_range = 
			( std::next(iter_start) == pool.end() ? ( mem.size() - 1 ) : std::next(iter_start)->second )
				- iter_start->second;
		
		if(entry_range >= target_offset){
			return std::make_pair(true, mem[(iter_start->second + target_offset)] );
			return addressReturnData(true, &(*iter_start), (iter_start->second + target_offset) == 0 );
		}
		return std::make_pair(false, nullptr);
	}
	
	template<typename AddressType, typename DataType>
		void SnapshotStorage<AddressType, DataType>::read
			(DataType* stream, AddressType address, std::size_t count, MemoryMap<AddressType, DataType>& main){
			DataType* streamIterator = stream;
			for (size_t i = 0; i< count; ++i){
				auto index = address + i;
				auto p = isAddressInSnapshot(index);
				*streamIterator++ = p.first ? *p.second : main.read(index);
			}
		}
	
	template<typename AddressType, typename DataType>
		void SnapshotStorage<AddressType, DataType>::write
			(DataType* stream, AddressType address, std::size_t count){
		
		}
	
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
