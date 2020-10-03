#ifndef _TEMP_UINT16
#define _TEMP_UINT16

#include "reg_base.hpp"

namespace BMMQ {

template<>
struct CPU_RegisterPair<uint16_t> :  public CPU_Register<uint16_t> {
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

}


#endif //_TEMP_UINT16