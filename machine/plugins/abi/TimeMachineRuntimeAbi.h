#ifndef TIME_MACHINE_RUNTIME_ABI_H
#define TIME_MACHINE_RUNTIME_ABI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TIME_MACHINE_RUNTIME_ABI_V2 2u
#define TIME_MACHINE_RUNTIME_ENTRY_V2 "time_get_machine_runtime_module_v2"
#define TIME_MACHINE_PREPARE_ROM 1u
#define TIME_MACHINE_PREPARE_BIOS 2u
#define TIME_MACHINE_PREPARE_STATE 3u
/* All callbacks are synchronous on one machine/control lane. No borrowed output,
 * C++ values, retained host callbacks or additional guest writers are allowed.
 * Return 1 for success, 0 for rejection. Exceptions must never cross this ABI. */
struct TimeMachineRetirementV2 {
    uint32_t struct_size, pc_before, pc_after, cycles;
    uint32_t segment_boundary, control_flow, execution_path, machine_boundary;
};
struct TimeMachineEventV2 {
    uint32_t struct_size, type, category, has_feedback;
    uint64_t step;
    uint16_t address;
    uint8_t value;
    struct TimeMachineRetirementV2 feedback;
    char message[96];
};
struct TimeMachineEventSinkV2 {
    uint32_t struct_size;
    void *context;
    int32_t (*emit)(void *, const struct TimeMachineEventV2 *);
};
struct TimeMachineVideoV2 {
    uint32_t struct_size, width, height, display_enabled, in_vblank;
    /* UINT32_MAX means no scanline. Pixels are tightly packed ARGB8888. */
    uint32_t scanline;
};
struct TimeMachineAudioV2 {
    uint32_t struct_size, sample_rate, channels, sample_count;
    uint64_t frame;
};
struct TimeMachineRegionV2 {
    uint32_t struct_size, category, readable, writable;
    uint16_t address, size;
    const char *label;
};
struct TimeMachineRuntimeV2 {
    uint32_t struct_size, abi_version;
    void *context;
    void (*destroy)(void *);
    /* prepare allocates a transaction without changing live state. Successful
     * commit is allocation-free and cannot fail; discard releases an unused
     * transaction. Both consume it. BIOS preparation is construction-only.
     * Tokens belong to this instance, cannot survive destruction, and are never
     * persisted. ROM/STATE bind the complete CPU, mapper and device state. */
    int32_t (*prepare)(void *, uint32_t operation, const uint8_t *, uint32_t, void **);
    void (*commit)(void *, void *);
    void (*discard)(void *, void *);
    /* Exactly one instruction and all hardware advancement, before return.
     * Events are ordered, bounded to 256 per retirement, copied by the sink.
     * A callback/sink failure faults the host; execution never silently falls
     * back or retries a possibly retired instruction. */
    int32_t (*step)(void *, struct TimeMachineRetirementV2 *, const struct TimeMachineEventSinkV2 *);
    int32_t (*read8)(void *, uint16_t, uint32_t inspection, uint8_t *);
    int32_t (*write8)(void *, uint16_t, uint8_t);
    int32_t (*read_register)(void *, const char *name, uint32_t width, uint16_t *);
    int32_t (*write_register)(void *, const char *name, uint32_t width, uint16_t);
    int32_t (*input)(void *, uint32_t mask);
    /* Host-owned output buffers; bounds are checked by both sides. A null state
     * buffer with capacity zero queries its size; reads do not mutate state.
     * Stable metadata/size across the query and copy is mandatory. */
    int32_t (*checkpoint)(void *, uint8_t *, uint32_t capacity, uint32_t *size);
    int32_t (*fingerprint)(void *, char *, uint32_t capacity);
    int32_t (*video)(void *, struct TimeMachineVideoV2 *, uint32_t *, uint32_t capacity);
    int32_t (*audio)(void *, struct TimeMachineAudioV2 *, int16_t *, uint32_t capacity);
};
struct TimeMachineRuntimeProviderV2 {
    uint32_t struct_size, abi_version;
    const char *id, *display_name, *family;
    uint32_t frame_width, frame_height, clock_hz;
    uint32_t region_count;
    const struct TimeMachineRegionV2 *regions;
    int32_t (*create)(struct TimeMachineRuntimeV2 *);
};
struct TimeMachineRuntimeModuleV2 {
    uint32_t struct_size, abi_version, provider_count;
    const char *id;
    const struct TimeMachineRuntimeProviderV2 *providers;
};
typedef const struct TimeMachineRuntimeModuleV2 *(*TimeGetMachineRuntimeModuleV2)(void);
#ifdef __cplusplus
}
#endif
#endif
