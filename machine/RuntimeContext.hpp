#ifndef BMMQ_RUNTIME_CONTEXT_HPP
#define BMMQ_RUNTIME_CONTEXT_HPP

#include <cstdint>
#include <utility>

#include "../inst_cycle/execute/executionBlock.hpp"
#include "../inst_cycle/fetch/fetchBlock.hpp"
#include "CPU.hpp"
#include "RegisterId.hpp"

namespace BMMQ {

namespace Plugin {
struct PluginMetadata;
}

enum class ExecutionGuarantee {
    BaselineFaithful,
    VisibleStatePreserving,
    Experimental,
};

struct RuntimeCapabilityProfile {
    bool interception = false;
    bool translation = false;
    bool invalidation = false;
    bool optimizationMetadata = false;
};

struct IInterceptionCapability {
    virtual ~IInterceptionCapability() = default;
};

struct ITranslationCapability {
    virtual ~ITranslationCapability() = default;
};

struct IInvalidationCapability {
    virtual ~IInvalidationCapability() = default;
};

struct IOptimizationMetadataCapability {
    virtual ~IOptimizationMetadataCapability() = default;
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
    virtual CpuFeedback step(FetchBlock& fetchBlock) {
        auto execBlock = decode(fetchBlock);
        execute(execBlock, fetchBlock);
        return getLastFeedback();
    }
    virtual CpuFeedback step() {
        auto fetchBlock = fetch();
        return step(fetchBlock);
    }
    virtual DataType read8(AddressType address) const = 0;
    virtual void write8(AddressType address, DataType value) = 0;
    virtual uint16_t read16(AddressType address) const {
        const auto lo = static_cast<uint16_t>(read8(address));
        const auto hi = static_cast<uint16_t>(read8(static_cast<AddressType>(address + 1)));
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    virtual void write16(AddressType address, uint16_t value) {
        write8(address, static_cast<DataType>(value & 0x00FFu));
        write8(static_cast<AddressType>(address + 1), static_cast<DataType>((value >> 8) & 0x00FFu));
    }
    virtual uint16_t readRegisterPair(RegisterId id) const = 0;
    virtual void writeRegisterPair(RegisterId id, uint16_t value) = 0;
    virtual void commitVisibleState() {}
    virtual const CpuFeedback& getLastFeedback() const = 0;
    virtual ExecutionGuarantee guarantee() const = 0;
    virtual const Plugin::PluginMetadata* attachedPolicyMetadata() const = 0;
    virtual RuntimeCapabilityProfile capabilityProfile() const = 0;
    virtual IInterceptionCapability* interceptionCapability() { return nullptr; }
    virtual ITranslationCapability* translationCapability() { return nullptr; }
    virtual IInvalidationCapability* invalidationCapability() { return nullptr; }
    virtual IOptimizationMetadataCapability* optimizationMetadataCapability() { return nullptr; }
};

} // namespace BMMQ

#endif // BMMQ_RUNTIME_CONTEXT_HPP
