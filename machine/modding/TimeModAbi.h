#ifndef TIME_MOD_ABI_H
#define TIME_MOD_ABI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TIME_MOD_ABI_V1 1u
#define TIME_MOD_ENTRYPOINT_V1 "time_get_mod_v1"
#if defined(_WIN32)
#define TIME_MOD_EXPORT __declspec(dllexport)
#else
#define TIME_MOD_EXPORT __attribute__((visibility("default")))
#endif
/* All callbacks are synchronous. Return 1 for success, 0 for failure.
 * No exceptions may cross this boundary. Strings are UTF-8, NUL terminated.
 * Host tables remain valid through destroy; buffers are borrowed for the call.
 * Call host functions only from a module callback on the owning thread.
 */
struct TimeModHostV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* context;
    int32_t (*find_region)(void*, const char* name, uint32_t* handle);
    int32_t (*read_region)(void*, uint32_t handle, uint64_t offset, uint8_t*, uint32_t size);
    int32_t (*write_region)(void*, uint32_t handle, uint64_t offset, const uint8_t*, uint32_t size);
    int32_t (*resolve_symbol)(void*, const char* name, uint32_t* bank, uint32_t* address);
};
struct TimeModCallV1 {
    uint32_t struct_size;
    uint32_t hook_id;
    uint64_t program_counter;
    uint64_t argument;
    uint64_t result;
};
struct TimeModApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* id;
    const char* version;
    uint32_t state_version;
    /* On failure leave *instance NULL, or provide an instance for destroy. */
    int32_t (*create)(const struct TimeModHostV1*, void** instance);
    void (*destroy)(void* instance);
    int32_t (*invoke)(void*, struct TimeModCallV1*);
    int32_t (*reset)(void*);
    /* Fixed per-instance state size, bounded by host; use size=0 for stateless. */
    uint32_t state_size;
    int32_t (*save)(void*, uint8_t* bytes, uint32_t size);
    /* Must validate before mutation; rejection leaves the instance unchanged. */
    int32_t (*restore)(void*, const uint8_t* bytes, uint32_t size);
};
typedef const struct TimeModApiV1* (*TimeGetModV1)(void);
#ifdef __cplusplus
}
#endif
#endif
