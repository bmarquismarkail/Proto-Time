namespace BMMQ {

template<typename T>
RegisterFile<T>::RegisterFile(const RegisterFile& other)
{
    for (const auto& entry : other.file) {
        file.push_back(RegisterEntry<T>{
            entry.name,
            entry.descriptor,
            entry.reg != nullptr ? entry.reg->clone() : nullptr,
        });
    }
    descriptors_ = other.descriptors_;
}

template<typename T>
RegisterFile<T>& RegisterFile<T>::operator=(const RegisterFile& other)
{
    if (this == &other) return *this;

    RegisterFile<T> copy(other);
    file = std::move(copy.file);
    descriptors_ = std::move(copy.descriptors_);
    return *this;
}

template<typename T>
const std::deque<RegisterEntry<T>>& RegisterFile<T>::entries() const noexcept
{
    return file;
}

template<typename T>
bool RegisterFile<T>::hasRegister(std::string_view id) const
{
    return findRegister(id) != nullptr;
}

template<typename T>
RegisterEntry<T>* RegisterFile<T>::findRegister(std::string_view id)
{
    for (auto& entry : file) {
        if (entry.name == id) return &entry;
    }
    return nullptr;
}

template<typename T>
const RegisterEntry<T>* RegisterFile<T>::findRegister(std::string_view id) const
{
    for (const auto& entry : file) {
        if (entry.name == id) return &entry;
    }
    return nullptr;
}

template<typename T>
bool RegisterFile<T>::hasDescriptor(std::string_view id) const
{
    return findDescriptor(id) != nullptr;
}

template<typename T>
RegisterDescriptor* RegisterFile<T>::findDescriptor(std::string_view id)
{
    for (auto& descriptor : descriptors_) {
        if (descriptor.name == id) return &descriptor;
    }
    return nullptr;
}

template<typename T>
const RegisterDescriptor* RegisterFile<T>::findDescriptor(std::string_view id) const
{
    for (const auto& descriptor : descriptors_) {
        if (descriptor.name == id) return &descriptor;
    }
    return nullptr;
}

template<typename T>
RegisterDescriptor& RegisterFile<T>::registerDescriptor(RegisterDescriptor descriptor)
{
    if (auto* existing = findDescriptor(descriptor.name)) {
        *existing = std::move(descriptor);
        return *existing;
    }
    descriptors_.push_back(std::move(descriptor));
    return descriptors_.back();
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::addRegister(const RegisterDescriptor& descriptor)
{
    auto descriptorCopy = descriptor;
    descriptorCopy.storage = RegisterStorage::RegisterFile;
    auto& storedDescriptor = registerDescriptor(std::move(descriptorCopy));

    if (auto* existing = findRegister(storedDescriptor.name)) {
        existing->name = storedDescriptor.name;
        existing->descriptor = storedDescriptor;
        if (!existing->reg || existing->reg->isPair() != storedDescriptor.isPair) {
            existing->reg = storedDescriptor.isPair
                ? std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_RegisterPair<T>>())
                : std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_Register<T>>());
        }
        return *existing;
    }

    auto reg = storedDescriptor.isPair
        ? std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_RegisterPair<T>>())
        : std::unique_ptr<CPU_Register<T>>(std::make_unique<CPU_Register<T>>());
    file.push_back(RegisterEntry<T>{storedDescriptor.name, storedDescriptor, std::move(reg)});
    return file.back();
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::addRegister(std::string_view id, bool isPair)
{
    return addRegister(RegisterDescriptor{std::string(id), RegisterWidth::Word16, RegisterStorage::RegisterFile, std::nullopt, isPair});
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::findOrInsert(const RegisterDescriptor& descriptor)
{
    auto* entry = findRegister(descriptor.name);
    if (entry != nullptr) return *entry;
    return addRegister(descriptor);
}

template<typename T>
RegisterEntry<T>& RegisterFile<T>::findOrInsert(std::string_view id, bool isPair)
{
    return findOrInsert(RegisterDescriptor{std::string(id), RegisterWidth::Word16, RegisterStorage::RegisterFile, std::nullopt, isPair});
}

} // namespace BMMQ
