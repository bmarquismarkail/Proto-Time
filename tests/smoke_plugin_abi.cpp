#include <cassert>
#include <string>

#include "gameboy/gameboy_plugin_runtime.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

int main()
{
    LR3592_PluginRuntime runtime;
    BMMQ::Plugin::DefaultStepPolicy policy;

    std::string error;
    const auto& runtimeMeta = runtime.metadata();
    const auto& policyMeta = policy.metadata();

    assert(BMMQ::Plugin::validateMetadata(runtimeMeta, &error));
    assert(error.empty());
    assert(BMMQ::Plugin::validateMetadata(policyMeta, &error));
    assert(error.empty());
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
    assert(!BMMQ::Plugin::validateMetadata(badSize, &error));
    assert(!error.empty());

    BMMQ::Plugin::PluginMetadata badAbi = runtimeMeta;
    badAbi.abiVersion = incompatibleMajor;
    assert(!BMMQ::Plugin::validateMetadata(badAbi, &error));
    assert(!error.empty());

    BMMQ::Plugin::PluginDescriptorV1 descriptor;
    assert(descriptor.structSize == sizeof(BMMQ::Plugin::PluginDescriptorV1));
    assert(BMMQ::Plugin::isAbiCompatible(descriptor.abiVersion));

    return 0;
}
