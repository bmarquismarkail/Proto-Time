#ifndef TIME_MACHINE_PROVIDER_ABI_H
#define TIME_MACHINE_PROVIDER_ABI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TIME_MACHINE_PROVIDER_ABI_V1 1u
#define TIME_MACHINE_PROVIDER_ENTRY_V1 "time_get_machine_provider_module_v1"
/* Synchronous construction API. Handles and this table expire when create returns.
 * No C++ objects cross the boundary. Factories construct complete admitted-family
 * machines through the host, optionally supplying a validated BIOS. Callbacks
 * return 1 on success, 0 on rejection. Host-owned result storage is transactional. */
struct TimeMachineFactoryHostV1 {
    uint32_t struct_size, abi_version;
    void *context;
    int32_t (*create_family)(void *, const char *family, uint64_t *handle);
    int32_t (*load_bios)(void *, uint64_t handle, const uint8_t *, uint32_t size);
};
/* Module state is construction/lifetime metadata, never guest execution state.
 * release must not throw, retain a host table, or access a destroyed machine. */
struct TimeMachineFactoryResultV1 {
    uint32_t struct_size;
    uint64_t handle;
    void *module_instance;
    void (*release)(void *);
};
struct TimeMachineProviderV1 {
    uint32_t struct_size, abi_version;
    const char *id, *display_name, *family;
    uint32_t frame_width, frame_height;
    int32_t (*create)(const struct TimeMachineFactoryHostV1 *, struct TimeMachineFactoryResultV1 *);
};
struct TimeMachineProviderModuleV1 {
    uint32_t struct_size, abi_version, provider_count;
    const char *id;
    const struct TimeMachineProviderV1 *providers;
};
typedef const struct TimeMachineProviderModuleV1 *(*TimeGetMachineProviderModuleV1)(void);
#ifdef __cplusplus
}
#endif
#endif
