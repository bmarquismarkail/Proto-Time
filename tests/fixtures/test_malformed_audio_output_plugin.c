#include "machine/plugins/abi/TimePluginAbi.h"

static const struct TimeAudioOutputApiV1 bad_api = {
    sizeof(struct TimeAudioOutputApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const struct TimePluginDescriptorV1 descriptor = {
    sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1,
    "test.audio-output.malformed", "Malformed Audio Output", &bad_api,
    sizeof(struct TimeAudioOutputApiV1)
};
static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &descriptor : 0;
}
static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.malformed-audio", "Malformed Audio Module", 1u, plugin_at
};
TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
