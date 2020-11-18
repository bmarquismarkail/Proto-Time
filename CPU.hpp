#ifndef BMMQ_CPU
#define BMMQ_CPU

#include <string_view>

#include "opcode.hpp"
#include "inst_cycle.hpp"
#include "memory/reg_base.hpp"

namespace BMMQ {
///////////////////////////////////////////////////////////////////////
// The CPU Class
template<typename AddressType, typename DataType, typename RegType>
class CPU {
public:
    virtual fetchBlock<AddressType, DataType> fetch()=0;
    virtual executionBlock<AddressType, DataType, RegType> decode(OpcodeList<AddressType, DataType, RegType> &oplist, fetchBlock<AddressType, DataType>& fetchData)=0;
    virtual void execute(const executionBlock<AddressType, DataType, RegType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb)=0;
};
}

#endif //BMMQ_CPU