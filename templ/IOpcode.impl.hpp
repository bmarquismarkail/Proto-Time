namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
IOpcode<AddressType, DataType, RegType>::IOpcode(const Imicrocode<AddressType, DataType, RegType>  *func)
{
    push_microcode(func);
}

template<typename AddressType, typename DataType, typename RegType>
IOpcode<AddressType, DataType, RegType>::IOpcode(const std::string id, microcodeFunc<AddressType, DataType, RegType>  *func)
{
    push_microcode(new Imicrocode(id, func) );
}

template<typename AddressType, typename DataType, typename RegType>
IOpcode<AddressType, DataType, RegType>::IOpcode(std::initializer_list<const Imicrocode<AddressType, DataType, RegType>  *> list)
    : microcode(list)
{
}

template<typename AddressType, typename DataType, typename RegType>
void IOpcode<AddressType, DataType, RegType>::push_microcode(const Imicrocode<AddressType, DataType, RegType>  *func)
{
    microcode.push_back(func);
}


template<typename AddressType, typename DataType, typename RegType>
void IOpcode<AddressType, DataType, RegType>::operator()(const MemorySnapshot<AddressType, DataType, RegType>& file)
{
    for(auto e : microcode) {
        (*e)(file);
    }
}
}