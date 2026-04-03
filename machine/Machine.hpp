#ifndef BMMQ_MACHINE_HPP
#define BMMQ_MACHINE_HPP

#include <cstdint>
#include <vector>

#include "RegisterId.hpp"
#include "RuntimeContext.hpp"

namespace BMMQ::Plugin {
class IExecutorPolicyPlugin;
}

namespace BMMQ {

class Machine {
public:
    virtual ~Machine() = default;
    Machine() = default;
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;
    virtual void loadRom(const std::vector<uint8_t>& bytes) = 0;
    virtual RuntimeContext& runtimeContext() = 0;
    virtual const RuntimeContext& runtimeContext() const = 0;
    virtual void attachExecutorPolicy(Plugin::IExecutorPolicyPlugin& policy) = 0;
    virtual const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const = 0;
    virtual ExecutionGuarantee guarantee() const noexcept {
        return runtimeContext().guarantee();
    }
    virtual void step() {
        runtimeContext().step();
    }
    virtual uint16_t readRegisterPair(std::string_view id) const = 0;
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_HPP
