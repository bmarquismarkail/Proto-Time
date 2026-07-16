#include "machine/plugins/abi/TimePluginAbi.h"

#include <cassert>
#include <cstring>

int main()
{
    static_assert(TIME_PLUGIN_ABI_VERSION_V1 == 1u);
    assert(std::strcmp(TIME_PLUGIN_MODULE_ENTRYPOINT_V1,
                       "time_get_plugin_module_v1") == 0);
    TimeFrontendApiV1 api{};
    api.struct_size = sizeof(api);
    api.abi_version = TIME_PLUGIN_ABI_VERSION_V1;
    assert(api.struct_size == sizeof(TimeFrontendApiV1));
    assert(api.abi_version == TIME_PLUGIN_ABI_VERSION_V1);

    return 0;
}
