#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "machine/plugins/abi/TimePluginAbi.h"

struct TestAudioOutput {
    struct TimeAudioOutputHostApiV1 host;
    struct TimeAudioOutputStatsV1 stats;
    uint32_t callback_samples;
    int32_t open;
};

static void* create_output(const struct TimeAudioOutputHostApiV1* host)
{
    struct TestAudioOutput* output;
    if (host == NULL || host->struct_size < sizeof(*host) ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
        host->drain_ready_audio == NULL) return NULL;
    output = (struct TestAudioOutput*)calloc(1u, sizeof(*output));
    if (output != NULL) {
        output->host = *host;
        output->stats.struct_size = sizeof(output->stats);
    }
    return output;
}

static void destroy_output(void* instance) { free(instance); }

static int32_t open_output(void* instance, const struct TimeAudioOutputConfigV1* config,
                           struct TimeAudioOutputDeviceInfoV1* obtained)
{
    struct TestAudioOutput* output = (struct TestAudioOutput*)instance;
    if (output == NULL || config == NULL || obtained == NULL ||
        config->struct_size < sizeof(*config) || obtained->struct_size < sizeof(*obtained) ||
        config->requested_sample_rate == 0u || config->requested_channels == 0u ||
        config->callback_samples == 0u) return 0;
    output->callback_samples = config->callback_samples;
    output->open = 1;
    output->stats.device_open = 1;
    obtained->sample_rate = config->requested_sample_rate;
    obtained->channels = config->requested_sample_rate == 12345u
        ? 3u : config->requested_channels;
    obtained->callback_samples = config->callback_samples;
    return 1;
}

static int32_t start_output(void* instance)
{
    struct TestAudioOutput* output = (struct TestAudioOutput*)instance;
    int16_t samples[4096];
    if (output == NULL || !output->open) return 0;
    if (output->callback_samples > 4096u) return 0;
    memset(samples, 0, output->callback_samples * sizeof(*samples));
    output->host.drain_ready_audio(output->host.host_context, samples,
                                   output->callback_samples);
    ++output->stats.callback_count;
    ++output->stats.start_count;
    output->stats.device_started = 1;
    return 1;
}

static void pause_output(void* instance)
{
    struct TestAudioOutput* output = (struct TestAudioOutput*)instance;
    if (output != NULL && output->stats.device_started) {
        ++output->stats.pause_count;
        output->stats.device_started = 0;
    }
}

static int32_t service_output(void* instance)
{
    struct TestAudioOutput* output = (struct TestAudioOutput*)instance;
    if (output == NULL || !output->open) return 0;
    ++output->stats.service_calls;
    return 1;
}

static void close_output(void* instance)
{
    struct TestAudioOutput* output = (struct TestAudioOutput*)instance;
    int16_t sample = 1;
    if (output == NULL) return;
    pause_output(output);
    output->open = 0;
    output->stats.device_open = 0;
    output->host.drain_ready_audio(output->host.host_context, &sample, 1u);
}

static const char* backend_name(const void* instance)
{
    return instance != NULL ? "pure-c-test-audio" : "";
}
static const char* last_error(const void* instance) { (void)instance; return ""; }
static int32_t query_stats(const void* instance, struct TimeAudioOutputStatsV1* stats)
{
    const struct TestAudioOutput* output = (const struct TestAudioOutput*)instance;
    if (output == NULL || stats == NULL || stats->struct_size < sizeof(*stats)) return 0;
    *stats = output->stats;
    return 1;
}

static const struct TimeAudioOutputApiV1 audio_api = {
    sizeof(struct TimeAudioOutputApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    create_output, destroy_output, open_output, start_output, pause_output,
    service_output, close_output, backend_name, last_error, query_stats
};
static const struct TimePluginDescriptorV1 descriptor = {
    sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1,
    "test.audio-output.pure-c", "Pure C Test Audio Output", &audio_api,
    sizeof(struct TimeAudioOutputApiV1)
};
static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &descriptor : NULL;
}
static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.c-audio-output", "C Audio Output Test Module", 1u, plugin_at
};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
