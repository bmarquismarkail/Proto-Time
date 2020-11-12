namespace BMMQ {

template<typename T>
T CPU_Register<T>::operator()()
{
    return value;
}

template<typename T>
T CPU_RegisterPair<T>::operator()()
{
	return value;
}

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
std::pair< std::string, CPU_Register<T>*>* RegisterFile<T>::findRegister(std::string_view id)
{
	for (auto& i : file)
		if (id.compare(i.first) == 0)
			return &i;
	return nullptr;
}
template<typename T>
void RegisterFile<T>::addRegister(const std::string id, bool isPair)
{
	if (!isPair)
		file.push_back(std::make_pair(id, new CPU_Register<T> {}));
	else
		file.push_back(std::make_pair(id, new CPU_RegisterPair<T> {}));
}

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

} // BMMQ