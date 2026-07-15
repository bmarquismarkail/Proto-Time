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
    TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1 = 1u
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
