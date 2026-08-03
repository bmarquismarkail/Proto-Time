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

struct TimeHostApiV1;

#define TIME_PLUGIN_ABI_VERSION_V1 1u
#define TIME_PLUGIN_MODULE_ENTRYPOINT_V1 "time_get_plugin_module_v1"

enum TimePluginKindV1 {
    TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1 = 1u,
    TIME_PLUGIN_KIND_FRONTEND_V1 = 2u,
    TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1 = 3u,
    TIME_PLUGIN_KIND_AUDIO_PROCESSOR_V1 = 4u,
    TIME_PLUGIN_KIND_IR_CORE_ADAPTER_V1 = 5u,
    TIME_PLUGIN_KIND_IR_EXECUTION_BACKEND_V1 = 6u
};

#define TIME_IR_ABI_VERSION_V1 1u

enum TimeIrResultV1 {
    TIME_IR_ERROR_V1 = -1,
    TIME_IR_DECLINED_V1 = 0,
    TIME_IR_OK_V1 = 1
};

enum TimeIrValueTypeV1 {
    TIME_IR_VALUE_VOID_V1 = 0u,
    TIME_IR_VALUE_BOOL_V1 = 1u,
    TIME_IR_VALUE_I8_V1 = 2u,
    TIME_IR_VALUE_I16_V1 = 3u,
    TIME_IR_VALUE_I32_V1 = 4u,
    TIME_IR_VALUE_I64_V1 = 5u
};

enum TimeIrOperandKindV1 {
    TIME_IR_OPERAND_VALUE_V1 = 0u,
    TIME_IR_OPERAND_IMMEDIATE_V1 = 1u,
    TIME_IR_OPERAND_GUEST_REGISTER_V1 = 2u,
    TIME_IR_OPERAND_GUEST_ADDRESS_V1 = 3u,
    TIME_IR_OPERAND_BLOCK_TARGET_V1 = 4u,
    TIME_IR_OPERAND_HELPER_V1 = 5u
};

enum TimeIrOpcodeV1 {
    TIME_IR_OPCODE_CONSTANT_V1 = 0u,
    TIME_IR_OPCODE_READ_REGISTER_V1 = 1u,
    TIME_IR_OPCODE_WRITE_REGISTER_V1 = 2u,
    TIME_IR_OPCODE_LOAD_MEMORY_V1 = 3u,
    TIME_IR_OPCODE_STORE_MEMORY_V1 = 4u,
    TIME_IR_OPCODE_ADD_V1 = 5u,
    TIME_IR_OPCODE_SUBTRACT_V1 = 6u,
    TIME_IR_OPCODE_MULTIPLY_V1 = 7u,
    TIME_IR_OPCODE_BIT_AND_V1 = 8u,
    TIME_IR_OPCODE_BIT_OR_V1 = 9u,
    TIME_IR_OPCODE_BIT_XOR_V1 = 10u,
    TIME_IR_OPCODE_SHIFT_LEFT_V1 = 11u,
    TIME_IR_OPCODE_SHIFT_RIGHT_LOGICAL_V1 = 12u,
    TIME_IR_OPCODE_SHIFT_RIGHT_ARITHMETIC_V1 = 13u,
    TIME_IR_OPCODE_BIT_NOT_V1 = 14u,
    TIME_IR_OPCODE_COMPARE_EQUAL_V1 = 15u,
    TIME_IR_OPCODE_COMPARE_NOT_EQUAL_V1 = 16u,
    TIME_IR_OPCODE_COMPARE_UNSIGNED_LESS_V1 = 17u,
    TIME_IR_OPCODE_COMPARE_SIGNED_LESS_V1 = 18u,
    TIME_IR_OPCODE_SELECT_V1 = 19u,
    TIME_IR_OPCODE_SET_PROGRAM_COUNTER_V1 = 20u,
    TIME_IR_OPCODE_BRANCH_V1 = 21u,
    TIME_IR_OPCODE_BRANCH_IF_V1 = 22u,
    TIME_IR_OPCODE_CALL_HELPER_V1 = 23u,
    TIME_IR_OPCODE_EXIT_V1 = 24u,
    TIME_IR_OPCODE_RETIRE_INSTRUCTION_V1 = 25u
};

enum TimeIrMemoryClassV1 {
    TIME_IR_MEMORY_GENERIC_V1 = 0u,
    TIME_IR_MEMORY_DIRECT_RAM_V1 = 1u,
    TIME_IR_MEMORY_READ_ONLY_V1 = 2u,
    TIME_IR_MEMORY_MMIO_V1 = 3u,
    TIME_IR_MEMORY_CARTRIDGE_V1 = 4u
};

enum TimeIrGuardKindV1 {
    TIME_IR_GUARD_MAPPING_GENERATION_V1 = 0u,
    TIME_IR_GUARD_CODE_BYTES_V1 = 1u,
    TIME_IR_GUARD_EXECUTION_STATE_V1 = 2u,
    TIME_IR_GUARD_HELPER_ABI_V1 = 3u
};

enum TimeIrBlockExitV1 {
    TIME_IR_BLOCK_EXIT_SEQUENTIAL_V1 = 0u,
    TIME_IR_BLOCK_EXIT_CONTROL_FLOW_V1 = 1u,
    TIME_IR_BLOCK_EXIT_INTERRUPT_BOUNDARY_V1 = 2u,
    TIME_IR_BLOCK_EXIT_UNSUPPORTED_V1 = 3u
};

struct TimeIrSourceInstructionV1 {
    uint32_t struct_size;
    uint64_t address;
    uint8_t bytes[4];
    uint8_t length;
};

struct TimeIrLoweringRequestV1 {
    uint32_t struct_size;
    uint32_t ir_abi_version;
    uint64_t mapping_generation;
    uint64_t execution_state;
    uint32_t instruction_count;
    const struct TimeIrSourceInstructionV1* instructions;
};

struct TimeIrOperandV1 {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t type;
    uint64_t payload;
};

struct TimeIrOperationV1 {
    uint32_t struct_size;
    uint32_t opcode;
    uint32_t result_id;
    uint32_t result_type;
    uint32_t memory_class;
    uint32_t operand_count;
    const struct TimeIrOperandV1* operands;
};

struct TimeIrGuardV1 {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t subject;
    uint64_t expected;
    uint64_t mask;
    uint32_t byte_count;
    const uint8_t* bytes;
};

struct TimeIrInstructionV1 {
    uint32_t struct_size;
    uint64_t address;
    uint32_t length;
    uint32_t cycles_not_taken;
    uint32_t cycles_taken;
    uint32_t taken_condition;
    uint32_t flags;
    uint32_t operation_count;
    const struct TimeIrOperationV1* operations;
};

enum TimeIrInstructionFlagV1 {
    TIME_IR_INSTRUCTION_CONTROL_FLOW_V1 = 1u << 0,
    TIME_IR_INSTRUCTION_INTERRUPT_SENSITIVE_V1 = 1u << 1
};

struct TimeIrBlockViewV1 {
    uint32_t struct_size;
    uint32_t ir_abi_version;
    uint64_t guest_start;
    uint64_t guest_end;
    uint64_t mapping_generation;
    uint32_t exit_kind;
    uint32_t guard_count;
    const struct TimeIrGuardV1* guards;
    uint32_t instruction_count;
    const struct TimeIrInstructionV1* instructions;
};

struct TimeIrBuilderV1 {
    uint32_t struct_size;
    uint32_t ir_abi_version;
    void* host_context;
    int32_t (*begin_block)(void* host_context, uint64_t guest_start,
                           uint64_t mapping_generation);
    int32_t (*add_guard)(void* host_context, const struct TimeIrGuardV1* guard);
    int32_t (*begin_instruction)(void* host_context,
                                 const struct TimeIrInstructionV1* instruction);
    int32_t (*emit_operation)(void* host_context,
                              const struct TimeIrOperationV1* operation);
    int32_t (*end_instruction)(void* host_context);
    int32_t (*finish_block)(void* host_context, uint32_t exit_kind);
};

struct TimeIrExecutionHostV1 {
    uint32_t struct_size;
    uint32_t ir_abi_version;
    void* host_context;
    uint64_t (*read_register)(void* host_context, uint32_t id, uint32_t type);
    int32_t (*write_register)(void* host_context, uint32_t id, uint32_t type,
                              uint64_t value);
    uint64_t (*load_memory)(void* host_context, uint64_t address, uint32_t type,
                            uint32_t memory_class);
    int32_t (*store_memory)(void* host_context, uint64_t address, uint32_t type,
                            uint32_t memory_class, uint64_t value);
    uint64_t (*call_helper)(void* host_context, uint32_t id, uint32_t result_type,
                            const uint64_t* arguments, uint32_t argument_count);
    int32_t (*set_program_counter)(void* host_context, uint64_t address);
};

struct TimeIrExecutionResultV1 {
    uint32_t struct_size;
    uint32_t branch_taken;
    uint32_t exit_requested;
    uint32_t cycle_condition;
    uint32_t retirement_reached;
};

struct TimeIrCoreAdapterApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t architecture_id;
    uint32_t ir_abi_version;
    void* (*create)(const struct TimeHostApiV1* host_api);
    void (*destroy)(void* instance);
    int32_t (*lower)(void* instance,
                     const struct TimeIrLoweringRequestV1* request,
                     const struct TimeIrBuilderV1* builder);
    int32_t (*validate_block)(const void* instance,
                              const struct TimeIrBlockViewV1* block);
    int32_t (*validate_execution_state)(const void* instance,
                                         const struct TimeIrBlockViewV1* block);
    const char* (*last_error)(const void* instance);
};

struct TimeIrExecutionBackendApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t architecture_id;
    uint32_t ir_abi_version;
    void* (*create)(const struct TimeHostApiV1* host_api);
    void (*destroy)(void* instance);
    void* (*compile)(void* instance, const struct TimeIrBlockViewV1* block);
    void (*destroy_artifact)(void* instance, void* artifact);
    int32_t (*execute)(void* instance, void* artifact, uint32_t instruction_index,
                       const struct TimeIrExecutionHostV1* host,
                       struct TimeIrExecutionResultV1* result);
    const char* (*last_error)(const void* instance);
};

enum TimeAudioProcessorResultV1 {
    TIME_AUDIO_PROCESSOR_ERROR_V1 = -1,
    TIME_AUDIO_PROCESSOR_BYPASS_V1 = 0,
    TIME_AUDIO_PROCESSOR_PROCESSED_V1 = 1
};

struct TimePsgVoiceV1 {
    uint32_t struct_size;
    uint8_t voice_id;
    uint8_t voice_kind;
};

struct TimePsgEventV1 {
    uint32_t struct_size;
    uint32_t sample_frame_offset;
    uint64_t sequence;
    uint32_t frequency_millihz;
    uint16_t level_q15;
    uint16_t raw_address;
    uint8_t voice_id;
    uint8_t voice_kind;
    uint8_t event_kind;
    uint8_t routing_mask;
    uint8_t timbre;
    uint8_t raw_value;
    uint8_t gate;
    uint8_t has_raw_write;
};

struct TimeAudioProcessorConfigV1 {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t max_block_samples;
    const char* config_json;
};

struct TimeAudioSourceBlockV1 {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t frame_counter;
    uint64_t first_sample_frame;
    uint64_t lifecycle_epoch;
    const int16_t* mixed_samples;
    uint32_t mixed_sample_count;
    const int16_t* voice_stems;
    uint32_t voice_stem_sample_count;
    const struct TimePsgVoiceV1* voices;
    uint32_t voice_count;
    const struct TimePsgEventV1* events;
    uint32_t event_count;
};

struct TimeAudioProcessorStatsV1 {
    uint32_t struct_size;
    uint64_t process_calls;
    uint64_t bypass_count;
    uint64_t error_count;
};

struct TimeAudioProcessorApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* (*create)(const struct TimeHostApiV1* host_api);
    void (*destroy)(void* instance);
    int32_t (*open)(void* instance, const struct TimeAudioProcessorConfigV1* config);
    int32_t (*process)(void* instance, const struct TimeAudioSourceBlockV1* input,
                       int16_t* output, uint32_t output_capacity_samples,
                       uint32_t* produced_samples);
    void (*flush)(void* instance, uint64_t lifecycle_epoch);
    void (*close)(void* instance);
    const char* (*last_error)(const void* instance);
    int32_t (*query_stats)(const void* instance, struct TimeAudioProcessorStatsV1* stats);
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
