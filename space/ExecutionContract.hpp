#pragma once
#include "Capture.hpp"
#include "memory/MemoryPool.hpp"
namespace BMMQ::Space {
// Internal optional execution contracts. Existing IMemory/plugin vtables are unchanged.
class RegisterExecutionView {
public:
    virtual ~RegisterExecutionView()=default;
    virtual RegisterFile<uint16_t>& executionRegisters()=0;
    virtual void publishRegisters()=0;
};
class ExecutionController {
public:
    virtual ~ExecutionController()=default;
    virtual void preflight()=0;
    virtual IMemory<uint16_t,uint8_t,uint16_t>* begin(std::span<const uint8_t>,uint64_t,
        MemoryPool<uint16_t,uint8_t,uint16_t>&,bool ramBlocked)=0;
    virtual void retired(Record&)=0;
    virtual void canonicalWrite(uint16_t,uint8_t)=0;
};
}
