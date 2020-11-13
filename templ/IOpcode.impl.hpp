namespace BMMQ {

template <typename regType>
IOpcode<regType>::IOpcode(const Imicrocode<regType>  *func)
{
    push_microcode(func);
}

template <typename regType>
IOpcode<regType>::IOpcode(const std::string id, microcodeFunc<regType>  *func)
{
    push_microcode(new Imicrocode(id, func) );
}

template <typename regType>
IOpcode<regType>::IOpcode(std::initializer_list<const Imicrocode<regType>  *> list)
    : microcode(list)
{
}

template <typename regType>
void IOpcode<regType>::push_microcode(const Imicrocode<regType>  *func)
{
    microcode.push_back(func);
}


template <typename regType>
void IOpcode<regType>::operator()(const RegisterFile<regType>& file)
{
    for(auto e : microcode) {
        (*e)(file);
    }
}
}