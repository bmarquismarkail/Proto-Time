#ifndef BMMQ_MACHINE_HPP
#define BMMQ_MACHINE_HPP

#include <cstdint>
#include <string_view>

namespace BMMQ {

class RuntimeContext;

class Machine {
public:
    virtual ~Machine() = default;
    virtual RuntimeContext& runtimeContext() = 0;
    virtual void step() = 0;
    virtual uint16_t readRegisterPair(std::string_view name) = 0;
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_HPP
