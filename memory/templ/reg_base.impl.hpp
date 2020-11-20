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
} // BMMQ