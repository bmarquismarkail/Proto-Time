#include <stdlib.h>

#include "machine/plugins/abi/TimePluginAbi.h"

struct TestPolicy {
    uint32_t marker;
};

static void* create_policy(const struct TimeHostApiV1* host_api)
{
    struct TestPolicy* policy;
    if (host_api == NULL || host_api->abi_version != TIME_PLUGIN_ABI_VERSION_V1) return NULL;
    policy = (struct TestPolicy*)malloc(sizeof(struct TestPolicy));
    if (policy != NULL) policy->marker = 0x54494d45u;
    return policy;
}

static void destroy_policy(void* instance)
{
    free(instance);
}

static uint32_t policy_backend(const void* instance)
{
    return instance != NULL ? TIME_EXECUTION_BACKEND_PORTABLE_IR_V1 : 0xffffffffu;
}

static uint32_t policy_guarantee(const void* instance)
{
    return instance != NULL ? TIME_EXECUTION_GUARANTEE_VISIBLE_STATE_PRESERVING_V1 : 0xffffffffu;
}

static uint32_t policy_capabilities(const void* instance)
{
    return instance != NULL
        ? TIME_RUNTIME_CAPABILITY_TRANSLATION_V1 | TIME_RUNTIME_CAPABILITY_INVALIDATION_V1
        : 0xffffffffu;
}

static int32_t policy_should_record(
    const void* instance, const struct TimeExecutionObservationV1* observation)
{
    return instance != NULL && observation != NULL && observation->fetch_address == 0x1234u;
}

static int32_t policy_should_segment(
    const void* instance, const struct TimeExecutionObservationV1* observation)
{
    return instance != NULL && observation != NULL &&
        (observation->flags & TIME_EXECUTION_OBSERVATION_SEGMENT_BOUNDARY_V1) != 0u;
}

static const struct TimeExecutorPolicyApiV1 policy_api = {
    sizeof(struct TimeExecutorPolicyApiV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    create_policy,
    destroy_policy,
    policy_backend,
    policy_guarantee,
    policy_capabilities,
    policy_should_record,
    policy_should_segment
};

static const struct TimePluginDescriptorV1 policy_descriptor = {
    sizeof(struct TimePluginDescriptorV1),
    TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1,
    "test.executor.c-portable-ir",
    "C Portable IR Test Policy",
    &policy_api,
    sizeof(struct TimeExecutorPolicyApiV1)
};

static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &policy_descriptor : NULL;
}

static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.c-executor",
    "C Executor Test Module",
    1u,
    plugin_at
};

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
