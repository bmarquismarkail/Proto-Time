#ifndef BMMQ_MACHINE_HPP
#define BMMQ_MACHINE_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "RuntimeContext.hpp"

namespace BMMQ {

class Machine {
public:
    virtual ~Machine() = default;
    virtual void loadRom(const std::vector<uint8_t>& bytes) = 0;
    virtual RuntimeContext& runtimeContext() = 0;
    virtual ExecutionGuarantee guarantee() {
        return runtimeContext().guarantee();
    }
    virtual void step() = 0;
    virtual uint16_t readRegisterPair(std::string_view name) = 0;
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_HPP
