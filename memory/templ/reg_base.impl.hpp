namespace BMMQ {

template<typename T>
T CPU_Register<T>::operator()() const
{
    return value;
}

template<typename T>
T CPU_RegisterPair<T>::operator()() const
{
    return this->value;
}
} // BMMQ
