namespace BMMQ {

template<typename regType>
std::map<std::string, microcodeFunc<regType>* > Imicrocode<regType>::v;

template<typename regType>
Imicrocode<regType>::Imicrocode(const std::string id, microcodeFunc<regType> *func)
{
    registerMicrocode(id, func);
}

template<typename regType>
const microcodeFunc<regType>* Imicrocode<regType>::findMicrocode(const std::string id)
{
    const auto i = v.find(id);
    if (i == v.end()) return nullptr;
    return i->second;

}

template<typename regType>
void Imicrocode<regType>::registerMicrocode(const std::string id, microcodeFunc<regType> *func)
{
    v.insert(std::make_pair (id, func) );
}

template<typename regType>
void Imicrocode<regType>::operator()(const RegisterFile<regType>& file) const
{
    for(auto e: v) {
        (*e.second)(file);
    }
}

}