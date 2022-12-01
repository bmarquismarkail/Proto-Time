namespace BMMQ {

	template<typename AddressType, typename DataType>
	DataType SnapshotStorage<AddressType, DataType>::Proxy::def;

	template<typename AddressType, typename DataType>
	SnapshotStorage<AddressType, DataType>::Proxy::Proxy(SnapshotStorage* p, AddressType a)
		:parent(p), address(a) {
	}

	template<typename AddressType, typename DataType>
	DataType& SnapshotStorage<AddressType, DataType>::Proxy::operator=(const DataType& rhs) {
		(*parent).at(address) = rhs;
		return (*parent).at(address);
	}

	template<typename AddressType, typename DataType>
	SnapshotStorage<AddressType, DataType>::Proxy::operator DataType& () {
		auto p = (*parent).isAddressInSnapshot(address);
		if (p.isAddressInSnapshot) {
			return (*parent).at(address);
		}
		else return def;
	}

}