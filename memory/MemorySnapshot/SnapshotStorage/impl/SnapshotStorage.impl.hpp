#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <limits>

namespace BMMQ {

	template<typename A, typename D>
	SnapshotAddressData<A, D>::SnapshotAddressData(bool retFlag, std::tuple< poolsizetype<A>, memindextype<D>, memindextype<D>> info)
		: isAddressInSnapshot(retFlag), info(info) {}

	template<typename AddressType, typename DataType>
	SnapshotAddressData<AddressType, DataType> SnapshotStorage<AddressType, DataType>::isAddressInSnapshot
	(AddressType at) {
		bool isAddressInSnapshot = false;
		auto entry_idx = 0;
		auto relofs = 0;
		auto rellength = 0;
		if (pool.empty());
		else if (at < pool.front().first) {
			rellength = at - pool.front().first;
		}
		// if the last entry doesn't have it, then none will:
		else if (at >= pool.back().first) {
			auto capacity = (mem.size() - pool.back().second);
			if (at < pool.back().first + capacity) {
				isAddressInSnapshot = true;
				relofs = at - pool.back().first;
				rellength = capacity - relofs;
			}
			else {
				relofs = capacity;
				rellength = at - (pool.back().first + capacity - 1);
			}
			entry_idx = pool.size() - 1;

		}
		// find the pair closest to but not past at
		// the read and write functions will sort if necessary
		// so all we need to do is check adjacent elements
		else {

			auto iter_start = std::prev(std::find_if(std::next(pool.begin()), std::prev(pool.end()), [&at](auto pe) {return at < pe.first; }));

			auto entry_size =
				(std::next(iter_start) == pool.end() ? (mem.size()) : std::next(iter_start)->second)
				- iter_start->second;

			isAddressInSnapshot = (at < (iter_start->first + entry_size));
			entry_idx = std::distance(pool.begin(), iter_start);
			if (isAddressInSnapshot) {
				relofs = at - iter_start->first;
				rellength = entry_size - relofs;
			}
			else {
				relofs = entry_size;
				rellength = at - (iter_start->first + entry_size - 1);
			}
		}

		return SnapshotAddressData<AddressType, DataType>(isAddressInSnapshot, std::make_tuple(entry_idx, relofs, rellength));
	}

	template<typename AddressType, typename DataType>
	SnapshotStorage<AddressType, DataType>::SnapshotStorage(MemoryMap<AddressType, DataType>& m)
	: map(m) {
	}

	template<typename AddressType, typename DataType>
	void SnapshotStorage<AddressType, DataType>::read
	(DataType* stream, AddressType address, AddressType count) {

		AddressType maxaddress = std::numeric_limits<AddressType>::max();
		AddressType bounds = maxaddress - address;
		count = std::min(bounds, count);
		if (count == 0) return;

		AddressType endAddress = address + count;
		maxAccessed = std::max(maxAccessed, endAddress);
		DataType* streamIterator = stream;
		AddressType index = address;

		while (count > 0) {
			auto p = isAddressInSnapshot(index);
			auto poolit = pool.begin();
			std::advance(poolit, std::get<0>(p.info));
			if (p.isAddressInSnapshot) {
				auto entrycap = std::get<2>(p.info);
				if (entrycap > count)
					entrycap = count;

				auto memit = mem.begin();
				std::advance(memit, poolit->second + std::get<1>(p.info));
				std::for_each_n(streamIterator, entrycap, [&memit](auto& s) {s = *memit; memit++; });
				streamIterator += entrycap;
				index += entrycap;
				count -= entrycap;
			}
			else {
				auto zerocount = count;
				if (std::next(poolit) != pool.end()) {
					auto nextaddress = std::next(poolit)->first;
					zerocount = nextaddress - index;
				}
				std::for_each_n(streamIterator, zerocount, [](auto& s) {s = 0; });
				streamIterator += zerocount;
				index += zerocount;
				count -= zerocount;
			}
		}

	}

	template<typename AddressType, typename DataType>
	void SnapshotStorage<AddressType, DataType>::write
	(DataType* stream, AddressType address, AddressType count) {

		AddressType maxaddress = std::numeric_limits<AddressType>::max();
		AddressType bounds = maxaddress - address;
		count = std::min(bounds, count);
		if (count == 0) return;

		AddressType endAddress = address + count;
		maxAccessed = std::max(maxAccessed, endAddress);
		auto memit = mem.begin();
		auto poolit = pool.begin();
		auto memindex = 0;
		if (mem.empty())
		{
			mem.insert(memit, stream, stream + count);
			pool.push_back(std::make_pair(address, 0));
			return;
		}

		auto p = isAddressInSnapshot(address);
		auto info = p.info;
		auto new_alloc_len = count;
		auto pool_index = std::get<0>(info);
		auto entrycap = std::get<2>(info);
		memindex = pool.at(pool_index).second + std::get<1>(info);
		if (!p.isAddressInSnapshot) {
			std::advance(poolit, pool_index);
			if (std::abs(entrycap) != 1) {
				poolit = pool.insert(((entrycap < 0) ? poolit : std::next(poolit)), std::make_pair(address, memindex));
			}
			std::for_each(std::next(poolit), pool.end(), [&new_alloc_len](auto& pe) {pe.second += new_alloc_len; });
		}
		else {
			std::advance(poolit, pool_index);
			if (count >= entrycap) {
				memindex = pool.at(pool_index).second + std::get<1>(info);
				auto endaddress = address + count - 1;
				auto address_return_data = isAddressInSnapshot(endaddress);
				auto address_return_info = address_return_data.info;
				auto delpoolit = pool.end();

				if (std::get<0>(address_return_info) + 1 != pool.size())
					std::advance(delpoolit, std::get<0>(address_return_info) - pool.size() + 1);

				if (delpoolit == pool.end()) {
					entrycap = mem.size() - memindex;
					pool.erase(std::next(poolit), delpoolit);
				}
				else if (delpoolit != poolit) {
					entrycap = delpoolit->second + std::get<1>(address_return_info) + 1;
					if (std::next(poolit) == delpoolit) pool.erase(delpoolit);
					else pool.erase(std::next(poolit), delpoolit);
				}

				new_alloc_len -= entrycap;
			}
		}
		auto streamit = stream;
		std::advance(memit, memindex);
		mem.insert(memit, streamit, stream + new_alloc_len);
		memit = mem.begin();
		std::advance(memit, new_alloc_len);
		stream += new_alloc_len;
		std::for_each_n(memit, count - new_alloc_len, [&stream](auto& d) {d = *stream++; });
		return;
	}

	template<typename AddressType, typename DataType>
	DataType& SnapshotStorage<AddressType, DataType>::at(AddressType idx) {
		auto p = isAddressInSnapshot(idx);
		auto memindex = pool.at(std::get<0>(p.info)).second + std::get<1>(p.info);
		if (p.isAddressInSnapshot)
			return mem[memindex];

		auto pool_index = std::get<0>(p.info);
		auto poolit = pool.begin();
		std::advance(poolit, pool_index);
		if (std::abs(std::get<2>(p.info)) != 1) {
			poolit = pool.insert(((std::get<2>(p.info) < 0) ? poolit : std::next(poolit)), std::make_pair(idx, memindex));
		}
		if (std::next(poolit) != pool.end() && std::get<1>(p.info) + std::get<2>(p.info) == std::next(poolit)->first) {
			pool.erase(std::next(poolit));
		}
		std::for_each(std::next(poolit), pool.end(), [](auto& pe) {pe.second++; });
		auto memit = mem.begin();
		std::advance(memit, memindex);
		memit = mem.insert(memit, 0);
		return *memit;
	}

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::Proxy SnapshotStorage<AddressType, DataType>::operator[](AddressType idx) {
		maxAccessed = std::max(maxAccessed, idx);
		return Proxy(this, idx);
	}
}
