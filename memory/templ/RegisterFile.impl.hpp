namespace BMMQ {

template<typename T>
RegisterFile<T>::RegisterFile(const RegisterFile& other)
{
    for (const auto& entry : other.file) {
        file.push_back(RegisterEntry<T>{
            entry.id,
            entry.reg != nullptr ? entry.reg->clone() : nullptr,
        });
    }
}

template<typename T>
RegisterFile<T>& RegisterFile<T>::operator=(const RegisterFile& other)
{
    if (this == &other) return *this;

    RegisterFile<T> copy(other);
    file = std::move(copy.file);
    return *this;
}

template<typename T>
const std::deque<RegisterEntry<T>>& RegisterFile<T>::entries() const noexcept
{
    return file;
}

template<typename T>
bool RegisterFile<T>::hasRegister(RegisterId id) const
{
    return findRegister(id) != nullptr;
}

template<typename T>
bool RegisterFile<T>::hasRegister(std::string_view id) const
{
    return hasRegister(registerIdFromString(id));
}

template<typename T>
RegisterEntry<T>* RegisterFile<T>::findRegister(RegisterId id)
{
    for (auto& entry : file) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

template<typename T>
const RegisterEntry<T>* RegisterFile<T>::findRegister(RegisterId id) const
{
    for (const auto& entry : file) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

template<typename T>
RegisterEntry<T>* RegisterFile<T>::findRegister(std::string_view id)
{
    return findRegister(registerIdFromString(id));
}

template<typename T>
const RegisterEntry<T>* RegisterFile<T>::findRegister(std::string_view id) const
{
    return findRegister(registerIdFromString(id));
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::addRegister(RegisterId id, bool isPair)
{
    auto reg = isPair
        ? std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_RegisterPair<T>>())
        : std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_Register<T>>());
    file.push_back(RegisterEntry<T>{id, std::move(reg)});
    return file.back();
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::addRegister(std::string_view id, bool isPair)
{
    return addRegister(registerIdFromString(id), isPair);
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::findOrInsert(RegisterId id, bool isPair)
{
    auto* entry = findRegister(id);
    if (entry != nullptr) return *entry;
    return addRegister(id, isPair);
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::findOrInsert(std::string_view id, bool isPair)
{
    return findOrInsert(registerIdFromString(id), isPair);
}

} // namespace BMMQ
