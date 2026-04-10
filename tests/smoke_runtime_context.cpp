#include <cassert>
#include <cstdint>

#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"
#include "machine/RuntimeContext.hpp"

namespace {

struct AdvancedInterception final : BMMQ::IInterceptionCapability {};
struct AdvancedTranslation final : BMMQ::ITranslationCapability {};
struct AdvancedInvalidation final : BMMQ::IInvalidationCapability {};
struct AdvancedOptimizationMetadata final : BMMQ::IOptimizationMetadataCapability {};

struct AdvancedRuntimeContext final : BMMQ::RuntimeContext {
    struct AdvancedPolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
        const BMMQ::Plugin::PluginMetadata& metadata() const override {
            static const BMMQ::Plugin::PluginMetadata meta{
                sizeof(BMMQ::Plugin::PluginMetadata),
                "bmmq.executor.policy.advanced",
                "Advanced Policy",
                BMMQ::Plugin::PluginKind::ExecutorPolicy,
                BMMQ::Plugin::kHostAbiVersion
            };
            return meta;
        }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::Experimental;
        }
        bool shouldRecord(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return true; }
        bool shouldSegment(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return false; }
    };

    BMMQ::CpuFeedback feedback{};
    AdvancedInterception interception;
    AdvancedTranslation translation;
    AdvancedInvalidation invalidation;
    AdvancedOptimizationMetadata optimization;
    AdvancedPolicy policy;

    FetchBlock fetch() override { return {}; }
    ExecutionBlock decode(FetchBlock&) override { return {}; }
    void execute(const ExecutionBlock&, FetchBlock&) override {}
    uint8_t read8(AddressType) const override { return 0; }
    void write8(AddressType, DataType) override {}
    uint8_t readRegister8(std::string_view) const override { return 0; }
    void writeRegister8(std::string_view, uint8_t) override {}
    uint16_t readRegister16(std::string_view) const override { return 0; }
    void writeRegister16(std::string_view, uint16_t) override {}
    const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
    uint32_t clockHz() const override { return 1000000u; }
    BMMQ::ExecutionGuarantee guarantee() const override {
        return BMMQ::ExecutionGuarantee::Experimental;
    }
    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return &policy.metadata(); }
    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return policy; }
    BMMQ::IInterceptionCapability* interceptionCapability() override { return &interception; }
    BMMQ::ITranslationCapability* translationCapability() override { return &translation; }
    BMMQ::IInvalidationCapability* invalidationCapability() override { return &invalidation; }
    BMMQ::IOptimizationMetadataCapability* optimizationMetadataCapability() override { return &optimization; }
    const BMMQ::IInterceptionCapability* interceptionCapability() const override { return &interception; }
    const BMMQ::ITranslationCapability* translationCapability() const override { return &translation; }
    const BMMQ::IInvalidationCapability* invalidationCapability() const override { return &invalidation; }
    const BMMQ::IOptimizationMetadataCapability* optimizationMetadataCapability() const override { return &optimization; }
};

}

int main()
{
    AdvancedRuntimeContext context;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy visibleStatePolicy;
    assert(visibleStatePolicy.guarantee() == BMMQ::ExecutionGuarantee::VisibleStatePreserving);
    assert(visibleStatePolicy.metadata().id == "bmmq.executor.policy.visible-state-preserving-step");
    assert(context.read16(0x0100) == 0x0000);
    context.write16(0xC000, 0x1234);
    context.commitVisibleState();
    assert(context.attachedExecutorPolicy().metadata().id == "bmmq.executor.policy.advanced");
    assert(context.capabilityProfile().interception);
    assert(context.capabilityProfile().translation);
    assert(context.capabilityProfile().invalidation);
    assert(context.capabilityProfile().optimizationMetadata);
    assert(context.interceptionCapability() != nullptr);
    assert(context.translationCapability() != nullptr);
    assert(context.invalidationCapability() != nullptr);
    assert(context.optimizationMetadataCapability() != nullptr);
    return 0;
}
