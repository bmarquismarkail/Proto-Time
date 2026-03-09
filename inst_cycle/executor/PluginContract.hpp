#ifndef PLUGIN_CONTRACT_HPP
#define PLUGIN_CONTRACT_HPP

#include <cstddef>
#include <cstdint>
#include <string>

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
    virtual bool shouldRecord(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
    virtual bool shouldSegment(const FetchBlock& fb, const BMMQ::CpuFeedback& feedback) const = 0;
};

inline bool validateMetadata(
    const PluginMetadata& metadata,
    std::string* error = nullptr) {
    if (metadata.structSize != sizeof(PluginMetadata)) {
        if (error != nullptr) *error = "plugin metadata size mismatch";
        return false;
    }
    if (metadata.id.empty()) {
        if (error != nullptr) *error = "plugin id is empty";
        return false;
    }
    if (!isAbiCompatible(metadata.abiVersion)) {
        if (error != nullptr) *error = "plugin ABI version is not compatible";
        return false;
    }
    return true;
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

    bool shouldRecord(const FetchBlock&, const BMMQ::CpuFeedback&) const override {
        return true;
    }

    bool shouldSegment(const FetchBlock&, const BMMQ::CpuFeedback& feedback) const override {
        return feedback.segmentBoundaryHint;
    }
};

} // namespace BMMQ::Plugin

#endif // PLUGIN_CONTRACT_HPP
