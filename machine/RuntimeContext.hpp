#ifndef BMMQ_RUNTIME_CONTEXT_HPP
#define BMMQ_RUNTIME_CONTEXT_HPP

#include <cstdint>
#include <limits>
#include <utility>

#include "../inst_cycle/execute/executionBlock.hpp"
#include "../inst_cycle/fetch/fetchBlock.hpp"
#include "CPU.hpp"
#include "ExecutionSlice.hpp"
#include "RegisterId.hpp"

namespace BMMQ {

namespace Plugin {
struct PluginMetadata;
class IExecutorPolicyPlugin;
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
    RuntimeContext() = default;
    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;
    RuntimeContext(RuntimeContext&&) = delete;
    RuntimeContext& operator=(RuntimeContext&&) = delete;

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
    virtual ExecutionSliceResult runSlice(
        const ExecutionBudget& budget,
        InstructionRetirementSink& retirementSink)
    {
        ExecutionSliceResult result;
        if (budget.maxInstructions == 0u) {
            result.exitReason = ExecutionSliceExitReason::InstructionBudget;
            return result;
        }
        if (budget.maxCycles == 0u) {
            result.exitReason = ExecutionSliceExitReason::CycleBudget;
            return result;
        }

        beginExecutionSlice(budget);
        struct ExecutionSliceScope final {
            RuntimeContext& context;
            ~ExecutionSliceScope() { context.endExecutionSlice(); }
        } scope{*this};

        while (result.progress.retiredInstructions < budget.maxInstructions &&
               result.progress.retiredCycles < budget.maxCycles) {
            result.lastFeedback = stepWithinExecutionSlice();
            ++result.progress.retiredInstructions;
            const auto cycles = static_cast<std::uint64_t>(result.lastFeedback.retiredCycles);
            if (cycles > std::numeric_limits<std::uint64_t>::max() -
                             result.progress.retiredCycles) {
                result.progress.retiredCycles = std::numeric_limits<std::uint64_t>::max();
            } else {
                result.progress.retiredCycles += cycles;
            }

            const auto retirement = retirementSink.retireInstruction(
                result.lastFeedback, result.progress);
            if (!retirement.continueExecution) {
                result.exitReason = retirement.exitReason;
                return result;
            }
            if (budget.stopOnSegmentBoundary && result.lastFeedback.segmentBoundaryHint) {
                result.exitReason = ExecutionSliceExitReason::SegmentBoundary;
                return result;
            }
            if (result.progress.retiredCycles >= budget.maxCycles) {
                result.exitReason = ExecutionSliceExitReason::CycleBudget;
                return result;
            }
        }

        result.exitReason = ExecutionSliceExitReason::InstructionBudget;
        return result;
    }
    virtual DataType read8(AddressType address) const = 0;
    virtual DataType peek8(AddressType address) const {
        return read8(address);
    }
    virtual void write8(AddressType address, DataType value) = 0;
    virtual uint16_t read16(AddressType address) const {
        const auto lo = static_cast<uint16_t>(read8(address));
        const auto hi = static_cast<uint16_t>(read8(static_cast<AddressType>(address + 1)));
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    virtual uint16_t peek16(AddressType address) const {
        const auto lo = static_cast<uint16_t>(peek8(address));
        const auto hi = static_cast<uint16_t>(peek8(static_cast<AddressType>(address + 1)));
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    virtual void write16(AddressType address, uint16_t value) {
        write8(address, static_cast<DataType>(value & 0x00FFu));
        write8(static_cast<AddressType>(address + 1), static_cast<DataType>((value >> 8) & 0x00FFu));
    }
    virtual uint8_t readRegister8(std::string_view id) const = 0;
    virtual void writeRegister8(std::string_view id, uint8_t value) = 0;
    virtual uint16_t readRegister16(std::string_view id) const = 0;
    virtual void writeRegister16(std::string_view id, uint16_t value) = 0;
    virtual uint16_t readRegisterPair(std::string_view id) const {
        return readRegister16(id);
    }
    virtual void writeRegisterPair(std::string_view id, uint16_t value) {
        writeRegister16(id, value);
    }
    virtual void commitVisibleState() {}
    virtual const CpuFeedback& getLastFeedback() const = 0;
    virtual uint32_t clockHz() const = 0;
    virtual ExecutionGuarantee guarantee() const = 0;
    virtual const Plugin::PluginMetadata* attachedPolicyMetadata() const = 0;
    virtual const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const = 0;
    virtual RuntimeCapabilityProfile capabilityProfile() const {
        return RuntimeCapabilityProfile{
            interceptionCapability() != nullptr,
            translationCapability() != nullptr,
            invalidationCapability() != nullptr,
            optimizationMetadataCapability() != nullptr,
        };
    }
    virtual IInterceptionCapability* interceptionCapability() { return nullptr; }
    virtual ITranslationCapability* translationCapability() { return nullptr; }
    virtual IInvalidationCapability* invalidationCapability() { return nullptr; }
    virtual IOptimizationMetadataCapability* optimizationMetadataCapability() { return nullptr; }
    virtual const IInterceptionCapability* interceptionCapability() const { return nullptr; }
    virtual const ITranslationCapability* translationCapability() const { return nullptr; }
    virtual const IInvalidationCapability* invalidationCapability() const { return nullptr; }
    virtual const IOptimizationMetadataCapability* optimizationMetadataCapability() const { return nullptr; }

protected:
    // Backends may retain slice-scoped dispatch state between instructions, but
    // every step must still return before the retirement sink is invoked. The
    // default path remains one ordinary RuntimeContext::step() per retirement.
    virtual void beginExecutionSlice(const ExecutionBudget&) {}
    virtual CpuFeedback stepWithinExecutionSlice() { return step(); }
    virtual void endExecutionSlice() noexcept {}
};

} // namespace BMMQ

#endif // BMMQ_RUNTIME_CONTEXT_HPP
