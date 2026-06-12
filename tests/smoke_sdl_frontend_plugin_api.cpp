#include "machine/plugins/SdlFrontendPlugin.hpp"

#include <cassert>
#include <cstring>

int main()
{
    static_assert(BMMQ::kSdlFrontendPluginApiVersion == 2u);
    static_assert(sizeof(BMMQ::SdlFrontendPluginApiV1) ==
                  BMMQ::SdlFrontendPluginApiV1{}.structSize);

    assert(std::strcmp(BMMQ::kSdlFrontendPluginApiEntryPoint,
                       "bmmq_get_sdl_frontend_plugin_api_v2") == 0);

    const BMMQ::SdlFrontendPluginApiV1 api{};
    assert(api.structSize == sizeof(BMMQ::SdlFrontendPluginApiV1));
    assert(api.apiVersion == BMMQ::kSdlFrontendPluginApiVersion);

    return 0;
}
