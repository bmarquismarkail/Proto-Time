#include "machine/modding/TimeModAbi.h"
#include <stdlib.h>
#include <string.h>
struct State { const struct TimeModHostV1* host; uint8_t counter; };
static int32_t create(const struct TimeModHostV1* host, void** output) {
    if (host->struct_size < sizeof(*host) || host->abi_version != TIME_MOD_ABI_V1) return 0;
    struct State* s = calloc(1, sizeof(*s));
    if (!s) return 0;
    s->host = host;
    *output = s;
#ifdef MOD_TEST_CREATE_FAIL
    return 0;
#endif
    return 1;
}
static void destroy(void* context) {
    struct State* s = context;
    uint32_t id = 0;
    const uint8_t value = 42;
    if (s->host->find_region(s->host->context, "pool", &id))
        s->host->write_region(s->host->context, id, 3, &value, 1);
    free(context);
}
static int32_t invoke(void* context, struct TimeModCallV1* call) {
    struct State* s = context;
    uint32_t id = 0, bank = 0, address = 0;
    uint8_t value = 0;
    if (call->hook_id == 1) {
        if (!s->host->find_region(s->host->context, "pool", &id)) return 0;
        if (!s->host->read_region(s->host->context, id, call->argument, &value, 1)) return 0;
        value++;
        if (!s->host->write_region(s->host->context, id, call->argument, &value, 1)) return 0;
        call->result = value;
        s->counter++;
        return 1;
    }
    if (call->hook_id == 2) {
        if (!s->host->resolve_symbol(s->host->context, "Target", &bank, &address)) return 0;
        call->result = ((uint64_t)bank << 16) | address;
        return 1;
    }
    if (call->hook_id == 3) {
        call->result = s->counter;
        return 1;
    }
    if (call->hook_id == 4)
        return s->host->read_region(s->host->context, (uint32_t)call->argument, 0, &value, 1);
    return 0;
}
static int32_t reset(void* context) { ((struct State*)context)->counter = 0; return 1; }
static int32_t save(void* context, uint8_t* output, uint32_t size) {
    if (size != 1) return 0;
    *output = ((struct State*)context)->counter;
    return 1;
}
static int32_t restore(void* context, const uint8_t* input, uint32_t size) {
    if (size != 1) return 0;
    ((struct State*)context)->counter = *input;
    return 1;
}
#ifndef MOD_TEST_ABI
#define MOD_TEST_ABI TIME_MOD_ABI_V1
#endif
#ifndef MOD_TEST_SIZE
#define MOD_TEST_SIZE sizeof(struct TimeModApiV1)
#endif
#ifdef MOD_TEST_MISSING_CALLBACK
#define MOD_TEST_INVOKE NULL
#else
#define MOD_TEST_INVOKE invoke
#endif
static const struct TimeModApiV1 api = {
    MOD_TEST_SIZE, MOD_TEST_ABI, "fixture", "1", 1,
    create, destroy, MOD_TEST_INVOKE, reset, 1, save, restore
};
TIME_MOD_EXPORT const struct TimeModApiV1* time_get_mod_v1(void) { (void)invoke; return &api; }
