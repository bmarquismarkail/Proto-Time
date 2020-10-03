#ifndef __REG_BASE
#define __REG_BASE

#include <string_view>

namespace BMMQ {

template<typename T>
struct _register {
    virtual T operator()()=0 ;
};

template<typename T>
struct CPU_Register :  public _register<T> {
    T value;
    T operator()()
    {
        return value;
    }
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

    T operator()()
    {
        return value;
    }
};

template<typename T>
class RegisterFile {
    std::vector< std::pair< std::string, CPU_Register<T>* >> file;
public:

    std::vector<std::pair< std::string, CPU_Register<T>* >>& operator()()
    {
        return file;
    }

    bool hasRegister(std::string id)
    {
        for (auto i: file)
            if ( id.compare(i.first) )
                return true;

        return false;
    }

    std::pair< std::string, CPU_Register<T>*> *findRegister(std::string_view id)
    {
        for (auto& i : file)
            if (id.compare(i.first) == 0)
                return &i;
        return nullptr;
    }

    void addRegister(const std::string id, bool isPair)
    {
        if (!isPair)
            file.push_back(std::make_pair(id, new CPU_Register<T> {}));
        else
            file.push_back(std::make_pair(id, new CPU_RegisterPair<T> {}));
    }

};

template<typename T>
class RegisterInfo {
    std::pair< std::string, CPU_Register<T>*> *info;
public:

    RegisterInfo() :info(new std::pair< std::string, CPU_Register<T>*>("", nullptr)) {};

    RegisterInfo(const std::string &id)
        :info(new std::pair< std::string, CPU_Register<T>*>(id, nullptr)) {}

    RegisterInfo(RegisterFile<T> &file, const std::string &id)
    {
        info = file.findRegister(id);
    }
    void registration(RegisterFile<T> &file, std::string_view id)
    {
        for (auto &i : file()) {
            if (i.first.compare(id) == 0)
                info = &i;
        }
    }

    CPU_Register<T> *operator()()
    {
        return info->second;
    }

    CPU_Register<T> *operator->()
    {
        return info->second;
    }

};

} // BMMQ

#endif // __REG_BASE