#ifndef BMMQ_CPU
#define BMMQ_CPU

#include <string_view>

#include "inst_cycle/execute/executionBlock.hpp"
#include "inst_cycle/fetch/fetchBlock.hpp"

namespace BMMQ {
///////////////////////////////////////////////////////////////////////
// The CPU Class
template<typename AddressType, typename DataType, typename RegType>
class CPU {
public:
    virtual fetchBlock<AddressType, DataType> fetch()=0;
    executionBlock<AddressType, DataType, RegType> 
        decode(auto &oplist, fetchBlock<AddressType, DataType>& fetchData);
    virtual void execute(const executionBlock<AddressType, DataType, RegType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb)=0;
};
}

#endif //BMMQ_CPU