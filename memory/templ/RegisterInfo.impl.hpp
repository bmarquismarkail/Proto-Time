#include <stdexcept>

namespace BMMQ {

template<typename T>
RegisterInfo<T>::RegisterInfo(RegisterFile<T>& file, std::string_view id)
    : name_(id)
{
    registration(file, id);
}

template<typename T>
RegisterInfo<T>::RegisterInfo(RegisterFile<T>& file, RegisterId id)
    : RegisterInfo(file, id.value)
{
}

template<typename T>
void RegisterInfo<T>::registration(RegisterFile<T>& file, std::string_view id)
{
    name_ = std::string(id);
    auto* entry = file.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    reg_ = entry->reg.get();
}

template<typename T>
void RegisterInfo<T>::registration(RegisterFile<T>& file, RegisterId id)
{
    registration(file, id.value);
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
std::string_view RegisterInfo<T>::getRegisterName() const noexcept
{
    return name_;
}

} // namespace BMMQ
