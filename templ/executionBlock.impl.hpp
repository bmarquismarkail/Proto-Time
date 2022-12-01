
namespace BMMQ {

template<typename AddressType, typename DataType, typename RegType>
std::vector<IOpcode<AddressType, DataType, RegType>>& executionBlock<AddressType, DataType, RegType>::getBlock()
{
    return code;
}

template<typename AddressType, typename DataType, typename RegType>
const std::vector<IOpcode<AddressType, DataType, RegType>>& executionBlock<AddressType, DataType, RegType>::getBlock() const
{
    return code;
}

template<typename AddressType, typename DataType, typename RegType>
CPU_Register<RegType>& executionBlock<AddressType, DataType, RegType>::emplaceRegister(std::string_view id, bool isPtr)
{

    auto check = snapshot.file.findRegister(id);
    if ( check != nullptr)
        return check;
    return (snapshot.file.addRegister(id, isPtr) ).second; ;

}

template<typename AddressType, typename DataType, typename RegType>
const MemorySnapshot<AddressType, DataType, RegType>& executionBlock<AddressType, DataType, RegType>::getMemory() const
{
    return snapshot;
}
}