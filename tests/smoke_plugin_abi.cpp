#include <cassert>
#include <stdexcept>
#include <string>

#include "gameboy/gameboy_plugin_runtime.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

int main()
{
    struct InvalidRuntime final : BMMQ::Plugin::ICpuCoreRuntime {
        const BMMQ::Plugin::PluginMetadata& metadata() const override {
            static const BMMQ::Plugin::PluginMetadata meta{
                0,
                "bmmq.core.invalid",
                "Invalid Runtime",
                BMMQ::Plugin::PluginKind::CpuCore,
                BMMQ::Plugin::kHostAbiVersion
            };
            return meta;
        }
        BMMQ::Plugin::FetchBlock fetch() override { return {}; }
        BMMQ::Plugin::ExecutionBlock decode(BMMQ::Plugin::FetchBlock&) override { return {}; }
        void execute(const BMMQ::Plugin::ExecutionBlock&, BMMQ::Plugin::FetchBlock&) override {}
        const BMMQ::CpuFeedback& getLastFeedback() const override {
            static BMMQ::CpuFeedback feedback{};
            return feedback;
        }
    };

    struct InvalidPolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
        const BMMQ::Plugin::PluginMetadata& metadata() const override {
            static const BMMQ::Plugin::PluginMetadata meta{
                sizeof(BMMQ::Plugin::PluginMetadata),
                "bmmq.executor.invalid",
                "Invalid Policy",
                BMMQ::Plugin::PluginKind::ExecutorPolicy,
                BMMQ::Plugin::AbiVersion{2, 0, 0}
            };
            return meta;
        }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::Experimental;
        }
        bool shouldRecord(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return true; }
        bool shouldSegment(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return false; }
    };

    LR3592_PluginRuntime runtime;
    BMMQ::Plugin::DefaultStepPolicy policy;

    const auto& runtimeMeta = runtime.metadata();
    const auto& policyMeta = policy.metadata();

    BMMQ::Plugin::validateMetadata(runtimeMeta);
    BMMQ::Plugin::validateMetadata(policyMeta);
    assert(runtimeMeta.kind == BMMQ::Plugin::PluginKind::CpuCore);
    assert(policyMeta.kind == BMMQ::Plugin::PluginKind::ExecutorPolicy);
    assert(policy.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);

    const BMMQ::Plugin::AbiVersion compatibleMinor{1, 0, 99};
    const BMMQ::Plugin::AbiVersion compatiblePatch{1, 0, 1};
    const BMMQ::Plugin::AbiVersion incompatibleMajor{2, 0, 0};
    const BMMQ::Plugin::AbiVersion incompatibleMinor{1, 1, 0};

    assert(BMMQ::Plugin::isAbiCompatible(compatibleMinor));
    assert(BMMQ::Plugin::isAbiCompatible(compatiblePatch));
    assert(!BMMQ::Plugin::isAbiCompatible(incompatibleMajor));
    assert(!BMMQ::Plugin::isAbiCompatible(incompatibleMinor));

    BMMQ::Plugin::PluginMetadata badSize = runtimeMeta;
    badSize.structSize = 0;
    bool badSizeThrew = false;
    try {
        BMMQ::Plugin::validateMetadata(badSize);
    } catch (const std::runtime_error&) {
        badSizeThrew = true;
    }
    assert(badSizeThrew);

    BMMQ::Plugin::PluginMetadata badAbi = runtimeMeta;
    badAbi.abiVersion = incompatibleMajor;
    bool badAbiThrew = false;
    try {
        BMMQ::Plugin::validateMetadata(badAbi);
    } catch (const std::runtime_error&) {
        badAbiThrew = true;
    }
    assert(badAbiThrew);

    BMMQ::Plugin::PluginDescriptorV1 descriptor;
    assert(descriptor.structSize == sizeof(BMMQ::Plugin::PluginDescriptorV1));
    assert(BMMQ::Plugin::isAbiCompatible(descriptor.abiVersion));

    InvalidRuntime invalidRuntime;
    bool invalidRuntimeThrew = false;
    try {
        BMMQ::Plugin::validateRuntimeStartup(invalidRuntime);
    } catch (const std::runtime_error&) {
        invalidRuntimeThrew = true;
    }
    assert(invalidRuntimeThrew);

    InvalidPolicy invalidPolicy;
    bool invalidPolicyThrew = false;
    try {
        BMMQ::Plugin::validateExecutorPolicyStartup(invalidPolicy);
    } catch (const std::runtime_error&) {
        invalidPolicyThrew = true;
    }
    assert(invalidPolicyThrew);

    return 0;
}
