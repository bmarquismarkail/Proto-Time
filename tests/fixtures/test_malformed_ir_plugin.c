#include "machine/plugins/abi/TimePluginAbi.h"

static const struct TimeIrCoreAdapterApiV1 bad_api = {
    sizeof(struct TimeIrCoreAdapterApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    0u, TIME_IR_ABI_VERSION_V1, NULL, NULL, NULL, NULL, NULL, NULL};

static const struct TimePluginDescriptorV1 descriptor = {
    sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_IR_CORE_ADAPTER_V1,
    "test.ir-adapter.bad", "Malformed IR Adapter", &bad_api, sizeof(bad_api)};

static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &descriptor : NULL;
}

static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.ir-bad", "Malformed IR Module", 1u, plugin_at};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
