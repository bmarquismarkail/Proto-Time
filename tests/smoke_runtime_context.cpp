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
    BMMQ::CpuFeedback feedback{};
    AdvancedInterception interception;
    AdvancedTranslation translation;
    AdvancedInvalidation invalidation;
    AdvancedOptimizationMetadata optimization;
    BMMQ::RuntimeCapabilityProfile profile{
        true,
        true,
        true,
        true
    };

    FetchBlock fetch() override { return {}; }
    ExecutionBlock decode(FetchBlock&) override { return {}; }
    void execute(const ExecutionBlock&, FetchBlock&) override {}
    uint8_t read8(AddressType) const override { return 0; }
    void write8(AddressType, DataType) override {}
    uint16_t readRegisterPair(BMMQ::RegisterId) const override { return 0; }
    void writeRegisterPair(BMMQ::RegisterId, uint16_t) override {}
    const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
    BMMQ::ExecutionGuarantee guarantee() const override {
        return BMMQ::ExecutionGuarantee::Experimental;
    }
    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return nullptr; }
    BMMQ::RuntimeCapabilityProfile capabilityProfile() const override { return profile; }
    BMMQ::IInterceptionCapability* interceptionCapability() override { return &interception; }
    BMMQ::ITranslationCapability* translationCapability() override { return &translation; }
    BMMQ::IInvalidationCapability* invalidationCapability() override { return &invalidation; }
    BMMQ::IOptimizationMetadataCapability* optimizationMetadataCapability() override { return &optimization; }
};

}

int main()
{
    AdvancedRuntimeContext context;
    assert(context.read16(0x0100) == 0x0000);
    context.write16(0xC000, 0x1234);
    context.commitVisibleState();
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
