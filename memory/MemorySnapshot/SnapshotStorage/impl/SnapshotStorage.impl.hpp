#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace BMMQ {

	template<typename A, typename D>
	SnapshotAddressData<A, D>::SnapshotAddressData(bool retFlag, std::tuple< poolsizetype<A>, memindextype<D>, memindextype<D>> info)
		: isAddressInSnapshot(retFlag), info(info) {}

	template<typename AddressType, typename DataType>
	SnapshotAddressData<AddressType, DataType> SnapshotStorage<AddressType, DataType>::isAddressInSnapshot
	(AddressType at) {
		static_assert(std::is_integral_v<AddressType>, "SnapshotStorage requires an integral AddressType.");
		using PoolSizeType = poolsizetype<AddressType>;
		using MemIndexType = memindextype<DataType>;
		using UnsignedAddressType = std::make_unsigned_t<AddressType>;
		static_assert(std::numeric_limits<UnsignedAddressType>::digits <= std::numeric_limits<std::uintmax_t>::digits,
			"SnapshotStorage address validation requires AddressType to fit in uintmax_t.");

		const auto checkedNegativeGapToMemIndex = [](AddressType lower, AddressType upper) -> MemIndexType {
			if constexpr (std::numeric_limits<AddressType>::is_signed) {
				if (lower < 0 || upper < 0) {
					throw std::out_of_range("SnapshotStorage does not support negative addresses.");
				}
			}

			const auto lowerUnsigned = static_cast<std::uintmax_t>(static_cast<UnsignedAddressType>(lower));
			const auto upperUnsigned = static_cast<std::uintmax_t>(static_cast<UnsignedAddressType>(upper));
			const auto gap = upperUnsigned - lowerUnsigned;
			const auto negativeLimit = static_cast<std::uintmax_t>(std::numeric_limits<MemIndexType>::max()) + 1u;
			if (gap > negativeLimit) {
				throw std::out_of_range("SnapshotStorage address gap exceeds memindextype range.");
			}
			if (gap == negativeLimit) {
				return std::numeric_limits<MemIndexType>::min();
			}
			return -static_cast<MemIndexType>(gap);
		};

		bool isAddressInSnapshot = false;
		PoolSizeType entry_idx = 0;
		MemIndexType relofs = 0;
		MemIndexType rellength = 0;
		if (pool.empty());
		else if (at < pool.front().first) {
			rellength = checkedNegativeGapToMemIndex(at, pool.front().first);
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
	SnapshotStorage<AddressType, DataType>::SnapshotStorage(MemoryStorage<AddressType, DataType>& m)
	: store(m), maxAccessed(0) {
	}

	template<typename AddressType, typename DataType>
	void SnapshotStorage<AddressType, DataType>::read
	(std::span<DataType> stream, AddressType address) {

		const auto maxaddress = static_cast<std::size_t>(std::numeric_limits<AddressType>::max());
		const auto addressOffset = static_cast<std::size_t>(address);
		const auto remainingAfterAddress = maxaddress - addressOffset;
		const auto remainingAddressCount = (remainingAfterAddress == std::numeric_limits<std::size_t>::max())
			? std::numeric_limits<std::size_t>::max()
			: remainingAfterAddress + 1u;
		std::size_t count = std::min(remainingAddressCount, stream.size());
		if (count == 0) return;

		AddressType endAddress = static_cast<AddressType>(address + static_cast<AddressType>(count - 1u));
		maxAccessed = std::max(maxAccessed, endAddress);
		if (pool.empty()) {
			store.read(stream.first(count), address);
			return;
		}
		DataType* streamIterator = stream.data();
		AddressType index = address;

		while (count > 0) {
			auto p = isAddressInSnapshot(index);
			auto poolit = pool.begin();
			std::advance(poolit, std::get<0>(p.info));
			if (p.isAddressInSnapshot) {
				auto entrycap = std::get<2>(p.info);
				if (static_cast<std::size_t>(entrycap) > count)
					entrycap = static_cast<memindextype<DataType>>(count);

				auto memit = mem.begin();
				std::advance(memit, poolit->second + std::get<1>(p.info));
				std::for_each_n(streamIterator, entrycap, [&memit](auto& s) {s = *memit; memit++; });
				streamIterator += entrycap;
				index = static_cast<AddressType>(index + static_cast<AddressType>(entrycap));
				count -= static_cast<std::size_t>(entrycap);
			}
			else {
				auto readcount = count;
				if (std::next(poolit) != pool.end()) {
					auto nextaddress = std::next(poolit)->first;
					readcount = static_cast<std::size_t>(nextaddress - index);
				}
				store.read(std::span<DataType>(streamIterator, readcount), index);
				streamIterator += readcount;
				index = static_cast<AddressType>(index + static_cast<AddressType>(readcount));
				count -= readcount;
			}
		}

	}

	template<typename AddressType, typename DataType>
	void SnapshotStorage<AddressType, DataType>::write
	(std::span<const DataType> stream, AddressType address) {

		const auto maxaddress = static_cast<std::size_t>(std::numeric_limits<AddressType>::max());
		const auto addressOffset = static_cast<std::size_t>(address);
		const auto remainingAfterAddress = maxaddress - addressOffset;
		const auto remainingAddressCount = (remainingAfterAddress == std::numeric_limits<std::size_t>::max())
			? std::numeric_limits<std::size_t>::max()
			: remainingAfterAddress + 1u;
		std::size_t count = std::min(remainingAddressCount, stream.size());
		if (count == 0) return;

		AddressType endAddress = static_cast<AddressType>(address + static_cast<AddressType>(count - 1u));
		maxAccessed = std::max(maxAccessed, endAddress);
		auto memit = mem.begin();
		auto poolit = pool.begin();
		auto memindex = 0;
		if (mem.empty())
		{
			mem.insert(memit, stream.begin(), stream.begin() + count);
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
			else if (entrycap < 0) {
				poolit->first = address;
			}
			std::for_each(std::next(poolit), pool.end(), [&new_alloc_len](auto& pe) {pe.second += new_alloc_len; });
		}
		else {
			std::advance(poolit, pool_index);
			if (count >= static_cast<std::size_t>(entrycap)) {
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
		std::advance(memit, memindex);
		mem.insert(memit, stream.begin(), stream.begin() + new_alloc_len);
		memit = mem.begin();
		std::advance(memit, memindex + new_alloc_len);
		auto streamIterator = stream.begin() + new_alloc_len;
		std::for_each_n(memit, count - new_alloc_len, [&streamIterator](auto& d) { d = *streamIterator++; });
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
