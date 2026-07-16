#include "machine/plugins/abi/TimePluginAbi.h"

static const struct TimePluginModuleV1 malformed_module = {
    sizeof(struct TimePluginModuleV1),
    TIME_PLUGIN_ABI_VERSION_V1,
    "",
    "Malformed test module",
    0u,
    0
};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &malformed_module;
}
