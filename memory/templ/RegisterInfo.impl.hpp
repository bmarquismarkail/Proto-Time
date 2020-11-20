namespace BMMQ {
template<typename T>
RegisterInfo<T>::RegisterInfo() :info(new std::pair< std::string, CPU_Register<T>*>("", nullptr)) {};

template<typename T>
RegisterInfo<T>::RegisterInfo(const std::string &id)
    :info(new std::pair< std::string, CPU_Register<T>*>(id, nullptr)) {}

template<typename T>
RegisterInfo<T>::RegisterInfo(RegisterFile<T> &file, const std::string &id)
{
    info = file.findRegister(id);
}
template<typename T>
void RegisterInfo<T>::registration(RegisterFile<T> &file, std::string_view id)
{
    for (auto &i : file()) {
        if (i.first.compare(id) == 0)
            info = &i;
    }
}

template<typename T>
CPU_Register<T>* RegisterInfo<T>::operator()()
{
    return info->second;
}

template<typename T>
CPU_Register<T>* RegisterInfo<T>::operator->()
{
    return info->second;
}

template<typename T>
std::string_view RegisterInfo<T>::getRegisterID() {
    return info->first;
}
}