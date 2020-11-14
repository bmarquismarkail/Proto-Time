#ifndef __REG_BASE
#define __REG_BASE

#include <string>
#include <string_view>
#include <utility>

namespace BMMQ {

template<typename T>
struct _register {
    virtual T operator()()=0 ;
};

template<typename T>
struct CPU_Register :  public _register<T> {
    T value;
    T operator()();
};

template<typename T>
struct CPU_RegisterPair :  public CPU_Register<T> {
    union {
        struct {
T lo:
            sizeof(T) * 4;
T hi:
            sizeof(T) * 4;
        };
        T value;
    };

    T operator()();
};

template<typename T>
class RegisterFile {
    std::vector< std::pair< std::string, CPU_Register<T>* >> file;
public:
    std::vector<std::pair< std::string, CPU_Register<T>* >>& operator()();
    bool hasRegister(std::string id);
    std::pair< std::string, CPU_Register<T>*>* findRegister(std::string_view id);
    std::pair< std::string, CPU_Register<T>*>* addRegister(const std::string id, bool isPair);
	std::pair< std::string, CPU_Register<T>*>* findOrInsert(const std::string id, bool isPair);
};

template<typename T>
class RegisterInfo {
    std::pair< std::string, CPU_Register<T>*> *info;
public:
    RegisterInfo();
    RegisterInfo(const std::string &id);
    RegisterInfo(RegisterFile<T> &file, const std::string &id);
    void registration(RegisterFile<T> &file, std::string_view id);
    CPU_Register<T> *operator()();
    CPU_Register<T> *operator->();
	std::string_view getRegisterID();
};

} // BMMQ

#include "reg_base.impl.hpp"
#endif // __REG_BASE