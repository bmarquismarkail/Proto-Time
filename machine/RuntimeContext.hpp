#ifndef BMMQ_RUNTIME_CONTEXT_HPP
#define BMMQ_RUNTIME_CONTEXT_HPP

#include <cstdint>

#include "../inst_cycle/execute/executionBlock.hpp"
#include "../inst_cycle/fetch/fetchBlock.hpp"
#include "CPU.hpp"

namespace BMMQ {

enum class ExecutionGuarantee {
    BaselineFaithful,
    VisibleStatePreserving,
    Experimental,
};

class RuntimeContext {
public:
    using AddressType = uint16_t;
    using DataType = uint8_t;
    using RegType = uint16_t;
    using FetchBlock = BMMQ::fetchBlock<AddressType, DataType>;
    using ExecutionBlock = BMMQ::executionBlock<AddressType, DataType, RegType>;

    virtual ~RuntimeContext() = default;
    virtual FetchBlock fetch() = 0;
    virtual ExecutionBlock decode(FetchBlock& fetchBlock) = 0;
    virtual void execute(const ExecutionBlock& block, FetchBlock& fetchBlock) = 0;
    virtual const CpuFeedback& getLastFeedback() const = 0;
    virtual ExecutionGuarantee guarantee() const = 0;
};

} // namespace BMMQ

#endif // BMMQ_RUNTIME_CONTEXT_HPP
