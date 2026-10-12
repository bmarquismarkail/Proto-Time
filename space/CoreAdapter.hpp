#pragma once
#include "CoreModel.hpp"
#include "ExecutionContract.hpp"
#include "machine/Machine.hpp"
namespace BMMQ::Space {
// Internal machine boundary, separate from external plugin ABIs.
class CoreAdapter {
public:
    virtual ~CoreAdapter()=default;
    virtual Machine& machine()=0;
    virtual const CoreModel& model()const=0;
    virtual MemoryPool<uint16_t,uint8_t,uint16_t>& memory()=0;
    virtual uint64_t location(uint16_t,bool write=false)const=0;
    virtual uint8_t rawRead(uint16_t)const=0;
    virtual bool ram(uint16_t)const=0;
    virtual uint16_t normalize(uint16_t)const=0;
    virtual size_t ramCapacity()const noexcept=0;
    virtual size_t ramIndex(uint16_t)const=0;
    virtual bool physicalRamRead(size_t,uint8_t&)const noexcept=0;
    virtual uint64_t physicalRamLocation(size_t)const=0;
    virtual size_t physicalRamIndex(uint64_t)const=0;
    virtual uint64_t fetchLocation(uint16_t pc,size_t offset)const{return location(uint16_t(pc+offset));}
    virtual uint16_t nextFetchAddress(uint16_t pc,size_t offset)const{return uint16_t(pc+offset);}
    virtual bool boundaryOnly()const=0;
    virtual bool opcodeSupported(uint8_t)const=0;
    virtual std::string fingerprint()const=0;
    virtual std::string romDigest()const=0;
    virtual void prepareInstruction(){}
    virtual void capture(Capture*)=0;
    virtual void execution(ExecutionController*)=0;
    virtual void input(uint8_t)=0;
    virtual std::unique_ptr<Machine> candidate(std::span<const uint8_t>)const=0;
};
std::unique_ptr<CoreAdapter> makeCoreAdapter(Machine&);
}
