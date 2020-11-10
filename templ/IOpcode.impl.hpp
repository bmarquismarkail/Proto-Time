namespace BMMQ {

template<typename AddressType, typename DataType>
void IOpcode::operator()(fetchBlock<AddressType, DataType> &fb)
{
    for(auto e : microcode) {
        (*e)();
    }
}
}