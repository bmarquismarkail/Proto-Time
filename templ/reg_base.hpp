#ifndef __REG_BASE
#define __REG_BASE

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
    std::vector< std::pair< std::string, CPU_Register<T> >> file;
public:

    std::pair<std::string, CPU_Register<T>> operator()()
    {
        return file;
    }
    std::pair< std::string, CPU_Register<T>> &findRegister(std::string_view id)
    {
        for (auto i : file)
            if (id.compare(i.first) )
                return i;
    }

    void addRegister(const std::string id, bool isPair)
    {
        if (!isPair)
            file.push_back(std::make_pair(id, CPU_Register<T> {}));
        else
            file.push_back(std::make_pair(id, CPU_RegisterPair<T> {}));
    }

};

template<typename T>
class RegisterInfo {
    // NOTE: This struct is designed to short-circuit to the register file
    // As such, references are not needed.
    std::pair< std::string, CPU_Register<T>* > info;
public:
    void registration(RegisterFile<T> &file, std::string_view id)
    {
        for (auto i : file.file) {
            if (i.first.compare(id))
                info.second = &i.second;
        }
    }

    CPU_Register<T> *operator()()
    {
        return info.second;
    }
};

} // BMMQ

#endif // __REG_BASE