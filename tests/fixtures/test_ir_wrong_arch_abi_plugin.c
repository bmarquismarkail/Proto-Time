#include "machine/plugins/abi/TimePluginAbi.h"

#include <stdlib.h>

#ifndef TEST_IR_WRONG_ARCHITECTURE
#define TEST_IR_WRONG_ARCHITECTURE 0u
#endif

#ifndef TEST_IR_INCOMPATIBLE_IR_ABI
#define TEST_IR_INCOMPATIBLE_IR_ABI TIME_IR_ABI_VERSION_V1
#endif

#define TEST_IR_ADAPTER_ID "test.ir-adapter.wrong-arch"
#define TEST_IR_BACKEND_ID "test.ir-backend.wrong-arch"

static void* create_instance(const struct TimeHostApiV1* host)
{
    (void)host;
    return calloc(1u, sizeof(void*));
}

static void destroy_instance(void* instance) { free(instance); }

static int32_t lower_nop(void* opaque, const struct TimeIrLoweringRequestV1* request,
                         const struct TimeIrBuilderV1* builder)
{
    (void)opaque; (void)request; (void)builder;
    return TIME_IR_OK_V1;
}

static int32_t validate_block(const void* opaque, const struct TimeIrBlockViewV1* block)
{
    (void)opaque; (void)block;
    return TIME_IR_OK_V1;
}

static int32_t validate_state(const void* opaque, const struct TimeIrBlockViewV1* block)
{
    (void)opaque; (void)block;
    return TIME_IR_OK_V1;
}

static const char* last_error(const void* opaque)
{
    (void)opaque;
    return "ok";
}

static void* compile_block(void* opaque, const struct TimeIrBlockViewV1* block)
{
    (void)opaque; (void)block;
    return calloc(1u, sizeof(void*));
}

static void destroy_artifact(void* opaque, void* artifact)
{
    (void)opaque;
    free(artifact);
}

static int32_t execute_block(void* opaque, void* raw_artifact, uint32_t instruction_index,
                             const struct TimeIrExecutionHostV1* host,
                             struct TimeIrExecutionResultV1* result)
{
    (void)opaque; (void)raw_artifact; (void)instruction_index; (void)host;
    if (result == NULL || result->struct_size < sizeof(*result)) return TIME_IR_ERROR_V1;
    result->branch_taken = 0u;
    result->exit_requested = 0u;
    result->cycle_condition = 0u;
    result->retirement_reached = 1u;
    return TIME_IR_OK_V1;
}

static const struct TimeIrCoreAdapterApiV1 adapter_api = {
    sizeof(struct TimeIrCoreAdapterApiV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    TEST_IR_WRONG_ARCHITECTURE,
    TEST_IR_INCOMPATIBLE_IR_ABI,
    create_instance,
    destroy_instance,
    lower_nop,
    validate_block,
    validate_state,
    last_error
};

static const struct TimeIrExecutionBackendApiV1 backend_api = {
    sizeof(struct TimeIrExecutionBackendApiV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    0u,
    TEST_IR_INCOMPATIBLE_IR_ABI,
    create_instance,
    destroy_instance,
    compile_block,
    destroy_artifact,
    execute_block,
    last_error
};

static const struct TimePluginDescriptorV1 descriptors[] = {
    {sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_IR_CORE_ADAPTER_V1,
     TEST_IR_ADAPTER_ID, "Wrong Arch/ABI Adapter", &adapter_api, sizeof(adapter_api)},
    {sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_IR_EXECUTION_BACKEND_V1,
     TEST_IR_BACKEND_ID, "Wrong Arch/ABI Backend", &backend_api, sizeof(backend_api)},
};

static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index < 2u ? &descriptors[index] : NULL;
}

static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.ir-wrong-arch-abi",
    "Wrong Arch/ABI IR Module",
    2u,
    plugin_at
};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
