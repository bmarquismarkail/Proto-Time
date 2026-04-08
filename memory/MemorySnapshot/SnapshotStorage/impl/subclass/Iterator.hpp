namespace BMMQ {

	template<typename AddressType, typename DataType>
	SnapshotStorage<AddressType, DataType>::iterator::iterator(SnapshotStorage* p)
		:parent(p) {
		address = 0;
	}

	template<typename AddressType, typename DataType>
	SnapshotStorage<AddressType, DataType>::iterator::iterator(AddressType a, SnapshotStorage* p)
		: address(a), parent(p) {
	}

	template<typename AddressType, typename DataType>
	DataType& SnapshotStorage<AddressType, DataType>::iterator::operator*() { return (*parent)[address]; }

	template<typename AddressType, typename DataType>
	DataType* SnapshotStorage<AddressType, DataType>::iterator::operator&() { return &(*parent)[address]; }

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::iterator& SnapshotStorage<AddressType, DataType>::iterator::operator++() {
		address++;
		return *this;
	}

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::iterator SnapshotStorage<AddressType, DataType>::iterator::operator++(int) {
		iterator tmp = *this;
		++(*this);
		return tmp;
	}

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::iterator SnapshotStorage<AddressType, DataType>::iterator::end() {
		auto lastAddress = (*parent).maxAccessed + 1;
		address = lastAddress;
		return *this;
	}

	template<typename AddressType, typename DataType>
	bool SnapshotStorage<AddressType, DataType>::iterator::operator== (const typename SnapshotStorage<AddressType, DataType>::iterator& rhs) {
		return (address == rhs.address);
	}

	template<typename AddressType, typename DataType>
	bool SnapshotStorage<AddressType, DataType>::iterator::operator!= (const typename SnapshotStorage<AddressType, DataType>::iterator& rhs) {
		return (address != rhs.address);
	}

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::iterator SnapshotStorage<AddressType, DataType>::begin() {
		return iterator(0, this);
	}

	template<typename AddressType, typename DataType>
	typename SnapshotStorage<AddressType, DataType>::iterator SnapshotStorage<AddressType, DataType>::end() {
		iterator endit(this);
		return endit.end();
	}

}