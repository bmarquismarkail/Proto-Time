#ifndef _TEMP_UINT16
#define _TEMP_UINT16

#include "reg_base.hpp"

template<>
struct BMMQ::CPU_RegisterPair<uint16_t> :  public BMMQ::CPU_Register<uint16_t> {
    union {
        struct {
            uint8_t lo;
            uint8_t hi;
        };
        uint16_t value;
    };

    uint16_t operator()()
    {
        return value;
    }
};

template<>
class BMMQ::RegisterFile<uint16_t> {
    std::vector< std::pair< std::string, CPU_Register<uint16_t>* >> file;
public:

    std::vector<std::pair< std::string, CPU_Register<uint16_t>* >>& operator()()
    {
        return file;
    }
    std::pair< std::string, CPU_Register<uint16_t>*> *findRegister(std::string_view id)
    {
        for (auto& i : file)
            if (id.compare(i.first) == 0)
                return &i;
        return nullptr;
    }
    void addRegister(const std::string id, bool isPair)
    {
        if (!isPair)
            file.push_back(std::make_pair(id, new CPU_Register<uint16_t> {}));
        else
            file.push_back(std::make_pair(id, new CPU_RegisterPair<uint16_t> {}));
    }

};

template<>
class BMMQ::RegisterInfo<uint16_t> {
    std::pair< std::string, CPU_Register<uint16_t>*> *info;
public:

    RegisterInfo() :info(new std::pair< std::string, CPU_Register<uint16_t>*>("", nullptr)) {};

    RegisterInfo(const std::string &id)
        :info(new std::pair< std::string, CPU_Register<uint16_t>*>(id, nullptr)) {}

    RegisterInfo(RegisterFile<uint16_t> &file, const std::string &id)
    {
        info = file.findRegister(id);
    }
    void registration(RegisterFile<uint16_t> &file, std::string_view id)
    {
        for (auto &i : file()) {
            if (i.first.compare(id) == 0)
                info = &i;
        }
    }

    CPU_Register<uint16_t> *operator()()
    {
        return info->second;
    }

    CPU_Register<uint16_t> *operator->()
    {
        return info->second;
    }
};

#endif //_TEMP_UINT16