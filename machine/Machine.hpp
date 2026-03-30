#ifndef BMMQ_MACHINE_HPP
#define BMMQ_MACHINE_HPP

namespace BMMQ {

class RuntimeContext;

class Machine {
public:
    virtual ~Machine() = default;
    virtual RuntimeContext& runtimeContext() = 0;
    virtual void step() = 0;
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_HPP
