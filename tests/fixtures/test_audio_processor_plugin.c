#include <stdint.h>
#include <stdlib.h>

#include "machine/plugins/abi/TimePluginAbi.h"

struct Processor {
    struct TimeAudioProcessorStatsV1 stats;
    int open;
    const char* last_error;
};

static void* create_processor(const struct TimeHostApiV1* host)
{
    struct Processor* processor;
    if (host == NULL || host->abi_version != TIME_PLUGIN_ABI_VERSION_V1) return NULL;
    processor = (struct Processor*)calloc(1u, sizeof(*processor));
    if (processor != NULL) processor->stats.struct_size = sizeof(processor->stats);
    return processor;
}

static void destroy_processor(void* instance) { free(instance); }

static int32_t open_processor(void* instance, const struct TimeAudioProcessorConfigV1* config)
{
    struct Processor* processor = (struct Processor*)instance;
    if (processor == NULL || config == NULL || config->sample_rate == 0u ||
        config->channels == 0u || config->max_block_samples == 0u) return 0;
    processor->open = 1;
    return 1;
}

static int32_t process_audio(void* instance, const struct TimeAudioSourceBlockV1* input,
                             int16_t* output, uint32_t capacity, uint32_t* produced)
{
    struct Processor* processor = (struct Processor*)instance;
    uint32_t i;
    if (processor == NULL || !processor->open || input == NULL || output == NULL ||
        produced == NULL || capacity < input->mixed_sample_count) return TIME_AUDIO_PROCESSOR_ERROR_V1;
    ++processor->stats.process_calls;
    if (input->voice_count == 0u || input->event_count == 0u ||
        input->voice_stem_sample_count == 0u) {
        ++processor->stats.error_count;
        processor->last_error = "rich audio metadata required";
        return TIME_AUDIO_PROCESSOR_ERROR_V1;
    }
    for (i = 0u; i < input->mixed_sample_count; ++i) {
        int32_t value = (int32_t)input->mixed_samples[i] + 100;
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        output[i] = (int16_t)value;
    }
    *produced = input->mixed_sample_count;
    return TIME_AUDIO_PROCESSOR_PROCESSED_V1;
}

static void flush_processor(void* instance, uint64_t epoch) { (void)instance; (void)epoch; }
static void close_processor(void* instance) { if (instance != NULL) ((struct Processor*)instance)->open = 0; }
static const char* last_error(const void* instance)
{
    const struct Processor* processor = (const struct Processor*)instance;
    return processor != NULL && processor->last_error != NULL ? processor->last_error : "";
}
static int32_t query_stats(const void* instance, struct TimeAudioProcessorStatsV1* stats)
{
    const struct Processor* processor = (const struct Processor*)instance;
    if (processor == NULL || stats == NULL || stats->struct_size < sizeof(*stats)) return 0;
    *stats = processor->stats;
    return 1;
}

static const struct TimeAudioProcessorApiV1 api = {
    sizeof(struct TimeAudioProcessorApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    create_processor, destroy_processor, open_processor, process_audio,
    flush_processor, close_processor, last_error, query_stats
};
static const struct TimePluginDescriptorV1 descriptor = {
    sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_AUDIO_PROCESSOR_V1,
    "test.audio-processor.rich", "Rich Audio Processor", &api, sizeof(api)
};
static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &descriptor : NULL;
}
static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.audio-processor", "Audio Processor Test", 1u, plugin_at
};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
