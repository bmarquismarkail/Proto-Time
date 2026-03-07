#ifndef PLUGIN_CONTRACT_HPP
#define PLUGIN_CONTRACT_HPP

#include <cstdint>
#include <string>

#include "../../CPU.hpp"
#include "../execute/executionBlock.hpp"
#include "../fetch/fetchBlock.hpp"

namespace BMMQ::Plugin {

inline constexpr uint32_t kPluginAbiVersion = 1;

enum class PluginKind : uint8_t {
    CpuCore = 0,
    ExecutorPolicy = 1,
    TraceCodec = 2
};

struct PluginMetadata {
    std::string id;
    std::string displayName;
    PluginKind kind = PluginKind::CpuCore;
    uint32_t abiVersion = kPluginAbiVersion;
};

using AddressType = uint16_t;
using DataType = uint8_t;
using RegType = uint16_t;

using FetchBlock = BMMQ::fetchBlock<AddressType, DataType>;
using ExecutionBlock = BMMQ::executionBlock<AddressType, DataType, RegType>;

class ICpuCoreRuntime {
public:
    virtual ~ICpuCoreRuntime() = default;
    virtual const PluginMetadata& metadata() const = 0;
    virtual FetchBlock fetch() = 0;
    virtual ExecutionBlock decode(FetchBlock& fb) = 0;
    virtual void execute(const ExecutionBlock& block, FetchBlock& fb) = 0;
    virtual const BMMQ::CpuFeedback& getLastFeedback() const = 0;
};

class IExecutorPolicyPlugin {
public:
    virtual ~IExecutorPolicyPlugin() = default;
    virtual const PluginMetadata& metadata() const = 0;
    virtual bool shouldRecord(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
    virtual bool shouldSegment(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
};

class DefaultStepPolicy final : public IExecutorPolicyPlugin {
public:
    const PluginMetadata& metadata() const override {
        static const PluginMetadata meta{
            "bmmq.executor.policy.default-step",
            "Default Step Policy",
            PluginKind::ExecutorPolicy,
            kPluginAbiVersion
        };
        return meta;
    }

    bool shouldRecord(const FetchBlock&, const BMMQ::CpuFeedback&) const override {
        return true;
    }

    bool shouldSegment(const FetchBlock&, const BMMQ::CpuFeedback& feedback) const override {
        return feedback.segmentBoundaryHint;
    }
};

} // namespace BMMQ::Plugin

#endif // PLUGIN_CONTRACT_HPP
