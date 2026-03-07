#ifndef BMMQ_CPU
#define BMMQ_CPU
	
#include <cstdint>
#include <string_view>

#include "inst_cycle/execute/executionBlock.hpp"
#include "inst_cycle/fetch/fetchBlock.hpp"

namespace BMMQ {

struct CpuFeedback {
    bool segmentBoundaryHint = false;
    bool isControlFlow = false;
    uint32_t pcBefore = 0;
    uint32_t pcAfter = 0;
};

///////////////////////////////////////////////////////////////////////
// The CPU Class
template<typename AddressType, typename DataType, typename RegType>
class CPU {
public:
    virtual fetchBlock<AddressType, DataType> fetch()=0;
    virtual executionBlock<AddressType, DataType, RegType>
        decode(fetchBlock<AddressType, DataType>& fetchData)=0;
    virtual void execute(const executionBlock<AddressType, DataType, RegType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb)=0;
    virtual const CpuFeedback& getLastFeedback() const = 0;
};
}

#endif //BMMQ_CPU
