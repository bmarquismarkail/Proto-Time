#include <stdlib.h>
#include <string.h>

#include "machine/plugins/abi/TimePluginAbi.h"

struct TestFrontend {
    struct TimeFrontendHostApiV1 host;
    struct TimeFrontendStatsV1 stats;
    int32_t ready;
};

static void* create_frontend(const struct TimeFrontendHostApiV1* host,
                             const struct TimeFrontendConfigV1* config)
{
    struct TestFrontend* frontend;
    if (host == NULL || config == NULL ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1) return NULL;
    frontend = (struct TestFrontend*)calloc(1u, sizeof(*frontend));
    if (frontend != NULL) {
        frontend->host = *host;
        frontend->stats.struct_size = sizeof(frontend->stats);
    }
    return frontend;
}

static void destroy_frontend(void* instance) { free(instance); }
static int32_t initialize_frontend(void* instance)
{
    struct TestFrontend* frontend = (struct TestFrontend*)instance;
    if (frontend == NULL) return 0;
    frontend->ready = 1;
    frontend->stats.backend_ready = 1;
    return 1;
}
static void shutdown_frontend(void* instance)
{
    struct TestFrontend* frontend = (struct TestFrontend*)instance;
    if (frontend != NULL) {
        frontend->ready = 0;
        frontend->stats.backend_ready = 0;
    }
}
static int32_t service_frontend(void* instance)
{
    struct TestFrontend* frontend = (struct TestFrontend*)instance;
    if (frontend == NULL || !frontend->ready) return 0;
    ++frontend->stats.service_calls;
    frontend->host.publish_digital_input(frontend->host.host_context,
                                         TIME_FRONTEND_INPUT_RIGHT_V1);
    frontend->host.request_quit(frontend->host.host_context);
    return 1;
}
static int32_t present_frontend(void* instance, const struct TimeFrontendFrameV1* frame)
{
    struct TestFrontend* frontend = (struct TestFrontend*)instance;
    if (frontend == NULL || !frontend->ready || frame == NULL || frame->pixels == NULL ||
        frame->pixel_format != TIME_FRONTEND_PIXEL_ARGB8888_V1) return 0;
    ++frontend->stats.frames_presented;
    return 1;
}
static void set_visible(void* instance, int32_t visible)
{
    struct TestFrontend* frontend = (struct TestFrontend*)instance;
    if (frontend != NULL) frontend->stats.window_visible = visible != 0;
}
static const char* backend_name(const void* instance)
{
    return instance != NULL ? "pure-c-test-frontend" : "";
}
static const char* last_error(const void* instance) { (void)instance; return ""; }
static int32_t query_stats(const void* instance, struct TimeFrontendStatsV1* stats)
{
    const struct TestFrontend* frontend = (const struct TestFrontend*)instance;
    if (frontend == NULL || stats == NULL ||
        stats->struct_size < sizeof(struct TimeFrontendStatsV1)) return 0;
    *stats = frontend->stats;
    return 1;
}

static const struct TimeFrontendApiV1 frontend_api = {
    sizeof(struct TimeFrontendApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    TIME_FRONTEND_CAPABILITY_VIDEO_V1 | TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1,
    create_frontend, destroy_frontend, initialize_frontend, shutdown_frontend,
    service_frontend, present_frontend, set_visible, backend_name, last_error, query_stats
};
static const struct TimePluginDescriptorV1 descriptor = {
    sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_FRONTEND_V1,
    "test.frontend.pure-c", "Pure C Test Frontend", &frontend_api,
    sizeof(struct TimeFrontendApiV1)
};
static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
    return index == 0u ? &descriptor : NULL;
}
static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.c-frontend", "C Frontend Test Module", 1u, plugin_at
};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
