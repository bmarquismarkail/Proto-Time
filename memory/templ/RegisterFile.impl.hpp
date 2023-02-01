namespace BMMQ {
template<typename T>
std::vector<std::pair< std::string, CPU_Register<T>* >>& RegisterFile<T>::operator()()
{
    return file;
}

template<typename T>
bool RegisterFile<T>::hasRegister(std::string id)
{
    for (auto i: file)
        if ( id.compare(i.first) )
            return true;

    return false;
}

template<typename T>
std::pair< std::string, CPU_Register<T>*> *RegisterFile<T>::findRegister(const std::string_view& id)
{
    for (auto& i : file)
        if (id.compare(i.first) == 0)
            return &i;
    return nullptr;
}

template<typename T>
std::pair< std::string, CPU_Register<T>*> *RegisterFile<T>::addRegister(const std::string& id, bool isPair)
{
    if (!isPair)
        file.push_back(std::make_pair(id, new CPU_Register<T> {}));
    else
        file.push_back(std::make_pair(id, new CPU_RegisterPair<T> {}));

    return &file.back();
}

template<typename T>
std::pair< std::string, CPU_Register<T>*> *RegisterFile<T>::findOrInsert(const std::string_view& id, bool isPair)
{
    auto pair = findRegister(id);
    if (pair == nullptr)
	{
		std::string newRegID{id};
        pair = addRegister(newRegID, isPair);
	}
    return pair;
}
}