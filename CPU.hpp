#ifndef BMMQ_CPU
#define BMMQ_CPU

#include <string_view>

#include "opcode.hpp"
#include "inst_cycle.hpp"
#include "inst_cycle.hpp"
#include "templ/reg_base.hpp"

namespace BMMQ {
///////////////////////////////////////////////////////////////////////
// The CPU Class
template<typename AddressType, typename DataType>
class CPU {
public:
    virtual fetchBlock<AddressType, DataType> fetch()=0;
    virtual executionBlock<AddressType> decode(OpcodeList &oplist, fetchBlock<AddressType, DataType>& fetchData)=0;
    virtual void execute(const executionBlock<AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb)=0;
};
}

#endif //BMMQ_CPU