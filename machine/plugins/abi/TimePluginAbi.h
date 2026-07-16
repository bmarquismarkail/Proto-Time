#ifndef TIME_PLUGIN_ABI_H
#define TIME_PLUGIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define TIME_PLUGIN_EXPORT __declspec(dllexport)
#else
#define TIME_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_PLUGIN_ABI_VERSION_V1 1u
#define TIME_PLUGIN_MODULE_ENTRYPOINT_V1 "time_get_plugin_module_v1"

enum TimePluginKindV1 {
    TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1 = 1u,
    TIME_PLUGIN_KIND_FRONTEND_V1 = 2u,
    TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1 = 3u
};

enum TimeFrontendCapabilityV1 {
    TIME_FRONTEND_CAPABILITY_VIDEO_V1 = 1u << 0,
    TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1 = 1u << 1,
    TIME_FRONTEND_CAPABILITY_WINDOW_V1 = 1u << 2
};

enum TimeFrontendConfigFlagV1 {
    TIME_FRONTEND_CONFIG_ENABLE_VIDEO_V1 = 1u << 0,
    TIME_FRONTEND_CONFIG_ENABLE_INPUT_V1 = 1u << 1,
    TIME_FRONTEND_CONFIG_CREATE_HIDDEN_V1 = 1u << 2,
    TIME_FRONTEND_CONFIG_SHOW_ON_PRESENT_V1 = 1u << 3
};

enum TimeFrontendInputButtonV1 {
    TIME_FRONTEND_INPUT_RIGHT_V1 = 0x01u,
    TIME_FRONTEND_INPUT_LEFT_V1 = 0x02u,
    TIME_FRONTEND_INPUT_UP_V1 = 0x04u,
    TIME_FRONTEND_INPUT_DOWN_V1 = 0x08u,
    TIME_FRONTEND_INPUT_BUTTON1_V1 = 0x10u,
    TIME_FRONTEND_INPUT_BUTTON2_V1 = 0x20u,
    TIME_FRONTEND_INPUT_META1_V1 = 0x40u,
    TIME_FRONTEND_INPUT_META2_V1 = 0x80u
};

enum TimeFrontendPixelFormatV1 {
    TIME_FRONTEND_PIXEL_ARGB8888_V1 = 1u
};

enum TimeFrontendRendererFlagV1 {
    TIME_FRONTEND_RENDERER_ACCELERATED_V1 = 1u << 0,
    TIME_FRONTEND_RENDERER_VSYNC_V1 = 1u << 1,
    TIME_FRONTEND_RENDERER_SOFTWARE_V1 = 1u << 2
};

enum TimeFrontendControlActionV1 {
    TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1 = 1u,
    TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1 = 2u,
    TIME_FRONTEND_CONTROL_SINGLE_STEP_V1 = 3u,
    TIME_FRONTEND_CONTROL_SPEED_UP_V1 = 4u,
    TIME_FRONTEND_CONTROL_SPEED_DOWN_V1 = 5u,
    TIME_FRONTEND_CONTROL_SAVE_STATE_V1 = 6u
};

enum TimeExecutionBackendV1 {
    TIME_EXECUTION_BACKEND_BASELINE_V1 = 0u,
    TIME_EXECUTION_BACKEND_CACHED_BLOCK_V1 = 1u,
    TIME_EXECUTION_BACKEND_PORTABLE_IR_V1 = 2u,
    TIME_EXECUTION_BACKEND_NATIVE_EXPERIMENTAL_V1 = 3u
};

enum TimeExecutionGuaranteeV1 {
    TIME_EXECUTION_GUARANTEE_BASELINE_FAITHFUL_V1 = 0u,
    TIME_EXECUTION_GUARANTEE_VISIBLE_STATE_PRESERVING_V1 = 1u,
    TIME_EXECUTION_GUARANTEE_EXPERIMENTAL_V1 = 2u
};

enum TimeRuntimeCapabilityV1 {
    TIME_RUNTIME_CAPABILITY_INTERCEPTION_V1 = 1u << 0,
    TIME_RUNTIME_CAPABILITY_TRANSLATION_V1 = 1u << 1,
    TIME_RUNTIME_CAPABILITY_INVALIDATION_V1 = 1u << 2,
    TIME_RUNTIME_CAPABILITY_OPTIMIZATION_METADATA_V1 = 1u << 3
};

enum TimeExecutionObservationFlagV1 {
    TIME_EXECUTION_OBSERVATION_CONTROL_FLOW_V1 = 1u << 0,
    TIME_EXECUTION_OBSERVATION_SEGMENT_BOUNDARY_V1 = 1u << 1
};

struct TimeHostApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    void (*log_message)(void* host_context, uint32_t level, const char* message);
};

struct TimeExecutionObservationV1 {
    uint32_t struct_size;
    uint16_t fetch_address;
    uint16_t pc_before;
    uint16_t pc_after;
    uint32_t retired_cycles;
    uint32_t flags;
};

struct TimeFrontendHostApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    void (*log_message)(void* host_context, uint32_t level, const char* message);
    void (*publish_digital_input)(void* host_context, uint32_t pressed_mask);
    void (*request_quit)(void* host_context);
    void (*request_control)(void* host_context, uint32_t action);
};

struct TimeFrontendConfigV1 {
    uint32_t struct_size;
    const char* window_title;
    uint32_t window_scale;
    int32_t frame_width;
    int32_t frame_height;
    uint32_t flags;
};

struct TimeFrontendFrameV1 {
    uint32_t struct_size;
    uint32_t pixel_format;
    int32_t width;
    int32_t height;
    uint32_t row_stride_bytes;
    const void* pixels;
    uint64_t generation;
    uint64_t lifecycle_epoch;
};

struct TimeFrontendStatsV1 {
    uint32_t struct_size;
    uint64_t service_calls;
    uint64_t events_processed;
    uint64_t frames_presented;
    uint64_t texture_recreate_count;
    uint64_t texture_upload_count;
    uint64_t present_failures;
    int64_t present_duration_last_ns;
    int64_t present_duration_high_water_ns;
    uint32_t renderer_flags;
    int32_t backend_ready;
    int32_t window_visible;
    int32_t quit_requested;
};

struct TimeFrontendApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t capabilities;
    void* (*create)(const struct TimeFrontendHostApiV1* host_api,
                    const struct TimeFrontendConfigV1* config);
    void (*destroy)(void* instance);
    int32_t (*initialize)(void* instance);
    void (*shutdown)(void* instance);
    int32_t (*service)(void* instance);
    int32_t (*present)(void* instance, const struct TimeFrontendFrameV1* frame);
    void (*set_window_visible)(void* instance, int32_t visible);
    const char* (*backend_name)(const void* instance);
    const char* (*last_error)(const void* instance);
    int32_t (*query_stats)(const void* instance, struct TimeFrontendStatsV1* stats);
};

struct TimeAudioOutputHostApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    uint32_t (*drain_ready_audio)(void* host_context,
                                  int16_t* output,
                                  uint32_t requested_samples);
};

struct TimeAudioOutputConfigV1 {
    uint32_t struct_size;
    uint32_t requested_sample_rate;
    uint32_t requested_channels;
    uint32_t callback_samples;
};

struct TimeAudioOutputDeviceInfoV1 {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t callback_samples;
};

struct TimeAudioOutputStatsV1 {
    uint32_t struct_size;
    uint64_t callback_count;
    uint64_t service_calls;
    uint64_t start_count;
    uint64_t pause_count;
    int32_t device_open;
    int32_t device_started;
};

struct TimeAudioOutputApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* (*create)(const struct TimeAudioOutputHostApiV1* host_api);
    void (*destroy)(void* instance);
    int32_t (*open)(void* instance,
                    const struct TimeAudioOutputConfigV1* config,
                    struct TimeAudioOutputDeviceInfoV1* obtained);
    int32_t (*start)(void* instance);
    void (*pause)(void* instance);
    int32_t (*service)(void* instance);
    void (*close)(void* instance);
    const char* (*backend_name)(const void* instance);
    const char* (*last_error)(const void* instance);
    int32_t (*query_stats)(const void* instance,
                           struct TimeAudioOutputStatsV1* stats);
};

struct TimeExecutorPolicyApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* (*create)(const struct TimeHostApiV1* host_api);
    void (*destroy)(void* instance);
    uint32_t (*backend)(const void* instance);
    uint32_t (*guarantee)(const void* instance);
    uint32_t (*required_capabilities)(const void* instance);
    int32_t (*should_record)(const void* instance,
                             const struct TimeExecutionObservationV1* observation);
    int32_t (*should_segment)(const void* instance,
                              const struct TimeExecutionObservationV1* observation);
};

struct TimePluginDescriptorV1 {
    uint32_t struct_size;
    uint32_t kind;
    const char* plugin_id;
    const char* display_name;
    const void* api;
    uint32_t api_size;
};

struct TimePluginModuleV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* module_id;
    const char* display_name;
    uint32_t plugin_count;
    const struct TimePluginDescriptorV1* (*plugin_at)(uint32_t index);
};

typedef const struct TimePluginModuleV1* (*TimeGetPluginModuleV1Fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_PLUGIN_ABI_H */
