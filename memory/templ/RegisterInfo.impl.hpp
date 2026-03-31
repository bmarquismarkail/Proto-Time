#include <stdexcept>

namespace BMMQ {

template<typename T>
RegisterInfo<T>::RegisterInfo(RegisterFile<T>& file, RegisterId id)
    : id_(id)
{
    registration(file, id);
}

template<typename T>
RegisterInfo<T>::RegisterInfo(RegisterFile<T>& file, std::string_view id)
    : RegisterInfo(file, registerIdFromString(id))
{
}

template<typename T>
void RegisterInfo<T>::registration(RegisterFile<T>& file, RegisterId id)
{
    id_ = id;
    auto* entry = file.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    reg_ = entry->reg.get();
}

template<typename T>
void RegisterInfo<T>::registration(RegisterFile<T>& file, std::string_view id)
{
    registration(file, registerIdFromString(id));
}

template<typename T>
CPU_Register<T>* RegisterInfo<T>::operator()() const
{
    if (reg_ == nullptr) {
        throw std::logic_error("register info is not bound");
    }
    return reg_;
}

template<typename T>
CPU_Register<T>* RegisterInfo<T>::operator->() const
{
    return operator()();
}

template<typename T>
RegisterId RegisterInfo<T>::getRegisterID() const noexcept
{
    return id_;
}

} // namespace BMMQ
