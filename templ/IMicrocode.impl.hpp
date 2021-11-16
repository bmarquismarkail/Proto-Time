namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
std::map<std::string, microcodeFunc<AddressType, DataType, RegType>* > Imicrocode<AddressType, DataType, RegType>::v;

template<typename AddressType, typename DataType, typename RegType>
Imicrocode<AddressType, DataType, RegType>::Imicrocode(const std::string id, microcodeFunc<AddressType, DataType, RegType> *func)
{
    registerMicrocode(id, func);
}

template<typename AddressType, typename DataType, typename RegType>
const microcodeFunc<AddressType, DataType, RegType>* Imicrocode<AddressType, DataType, RegType>::findMicrocode(const std::string id)
{
    const auto i = v.find(id);
    if (i == v.end()) return nullptr;
    return i->second;

}

template<typename AddressType, typename DataType, typename RegType>
void Imicrocode<AddressType, DataType, RegType>::registerMicrocode(const std::string id, microcodeFunc<AddressType, DataType, RegType> *func)
{
    v.insert(std::make_pair (id, func) );
}

template<typename AddressType, typename DataType, typename RegType>
void Imicrocode<AddressType, DataType, RegType>::operator()(const MemorySnapshot<AddressType, DataType, RegType>& file) const
{
    for(auto e: v) {
        (*e.second)(file);
    }
}

}