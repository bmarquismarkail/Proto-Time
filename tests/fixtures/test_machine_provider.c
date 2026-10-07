#include "machine/plugins/abi/TimeMachineProviderAbi.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#if defined(__GNUC__)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif
static _Atomic unsigned releases;
EXPORT unsigned time_test_provider_releases(void) { return atomic_load(&releases); }
static void release(void *p) { atomic_fetch_add(&releases,1); free(p); }
static int make(const struct TimeMachineFactoryHostV1 *h, struct TimeMachineFactoryResultV1 *r, const char *family) {
    uint64_t handle = 0;
    const char *mode = getenv("TIME_TEST_PROVIDER_MODE");
    if (h->struct_size != sizeof(*h) || h->abi_version != 1) return 0;
    if (!h->create_family(h->context, family, &handle)) return 0;
    r->handle = handle;
    r->module_instance = malloc(1);
    r->release = release;
    if (!r->module_instance) return 0;
    if (mode && !strcmp(mode,"bad-handle")) r->handle++;
    if (mode && !strcmp(mode,"decline")) return 0;
    if (mode && !strcmp(mode,"cross-family")) {
        uint64_t unused; h->create_family(h->context,"third",&unused);
    }
    if (mode && !strcmp(mode,"bad-bios")) {
        h->load_bios(h->context,handle,0,257);
    }
    if (mode && !strcmp(mode,"bios")) {
        uint8_t bios[256] = {0x3e,0x42};
        if (!h->load_bios(h->context,handle,bios,sizeof(bios))) return 0;
    }
    return 1;
}
static int gb(const struct TimeMachineFactoryHostV1 *h, struct TimeMachineFactoryResultV1 *r) { return make(h,r,"gameboy"); }
static int gg(const struct TimeMachineFactoryHostV1 *h, struct TimeMachineFactoryResultV1 *r) { return make(h,r,"gamegear"); }
EXPORT const struct TimeMachineProviderModuleV1 *time_get_machine_provider_module_v1(void) {
    static struct TimeMachineProviderV1 providers[2];
    static struct TimeMachineProviderModuleV1 module;
    const char *mode = getenv("TIME_TEST_PROVIDER_MODE");
    providers[0]=(struct TimeMachineProviderV1){sizeof(providers[0]),1,"external-gb","External Game Boy","gameboy",160,144,gb};
    providers[1]=(struct TimeMachineProviderV1){sizeof(providers[1]),1,"external-gg","External Game Gear","gamegear",160,144,gg};
    module=(struct TimeMachineProviderModuleV1){sizeof(module),1,2,"fixture-machines",providers};
    if (mode && !strcmp(mode,"version")) module.abi_version=2;
    if (mode && !strcmp(mode,"count")) module.provider_count=65;
    if (mode && !strcmp(mode,"size")) providers[1].struct_size=0;
    if (mode && !strcmp(mode,"callback")) providers[1].create=0;
    if (mode && !strcmp(mode,"duplicate")) providers[1].id=providers[0].id;
    if (mode && !strcmp(mode,"unknown")) providers[1].family="third";
    if (mode && !strcmp(mode,"string")) providers[1].id="bad/id";
    return &module;
}
