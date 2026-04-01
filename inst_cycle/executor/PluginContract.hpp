#ifndef PLUGIN_CONTRACT_HPP
#define PLUGIN_CONTRACT_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "../../machine/RuntimeContext.hpp"
#include "../../machine/CPU.hpp"
#include "../execute/executionBlock.hpp"
#include "../fetch/fetchBlock.hpp"

namespace BMMQ::Plugin {

struct AbiVersion {
    uint16_t major = 1;
    uint16_t minor = 0;
    uint16_t patch = 0;
};

inline constexpr AbiVersion kHostAbiVersion {1, 0, 0};

inline constexpr bool isAbiCompatible(
    const AbiVersion& pluginAbi,
    const AbiVersion& hostAbi = kHostAbiVersion) {
    return pluginAbi.major == hostAbi.major && pluginAbi.minor <= hostAbi.minor;
}

enum class PluginKind : uint8_t {
    CpuCore = 0,
    ExecutorPolicy = 1,
    TraceCodec = 2
};

struct PluginMetadata {
    std::size_t structSize = sizeof(PluginMetadata);
    std::string id;
    std::string displayName;
    PluginKind kind = PluginKind::CpuCore;
    AbiVersion abiVersion = kHostAbiVersion;
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
    virtual BMMQ::ExecutionGuarantee guarantee() const = 0;
    virtual BMMQ::RuntimeCapabilityProfile capabilityProfile() const {
        return {};
    }
    virtual bool shouldRecord(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
    virtual bool shouldSegment(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
};

inline void validateMetadata(const PluginMetadata& metadata) {
    if (metadata.structSize != sizeof(PluginMetadata)) {
        throw std::runtime_error("plugin metadata size mismatch");
    }
    if (metadata.id.empty()) {
        throw std::runtime_error("plugin id is empty");
    }
    if (!isAbiCompatible(metadata.abiVersion)) {
        throw std::runtime_error("plugin ABI version is not compatible");
    }
}

inline void validateRuntimeStartup(const ICpuCoreRuntime& runtime) {
    validateMetadata(runtime.metadata());
    if (runtime.metadata().kind != PluginKind::CpuCore) {
        throw std::runtime_error("runtime plugin kind is not CpuCore");
    }
}

inline void validateExecutorPolicyStartup(const IExecutorPolicyPlugin& policy) {
    validateMetadata(policy.metadata());
    if (policy.metadata().kind != PluginKind::ExecutorPolicy) {
        throw std::runtime_error("executor policy kind is not ExecutorPolicy");
    }
}

// C ABI entrypoints for future dynamic plugin loading.
// A shared library can export `bmmq_get_plugin_descriptor_v1`.
struct PluginDescriptorV1 {
    std::size_t structSize = sizeof(PluginDescriptorV1);
    AbiVersion abiVersion = kHostAbiVersion;
    PluginKind kind = PluginKind::CpuCore;
    const char* pluginId = nullptr;
    const char* displayName = nullptr;

    // Factory-style opaque creation hooks.
    void* (*create)() = nullptr;
    void (*destroy)(void*) = nullptr;
};

class DefaultStepPolicy final : public IExecutorPolicyPlugin {
public:
    const PluginMetadata& metadata() const override {
        static const PluginMetadata meta{
            sizeof(PluginMetadata),
            "bmmq.executor.policy.default-step",
            "Default Step Policy",
            PluginKind::ExecutorPolicy,
            kHostAbiVersion
        };
        return meta;
    }

    BMMQ::ExecutionGuarantee guarantee() const override {
        return BMMQ::ExecutionGuarantee::BaselineFaithful;
    }

    BMMQ::RuntimeCapabilityProfile capabilityProfile() const override {
        return {};
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
