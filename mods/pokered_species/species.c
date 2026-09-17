#include "machine/modding/TimeModAbi.h"
#include <stdlib.h>

struct State {
    const struct TimeModHostV1* host;
    uint32_t selection, records, current;
};
static int32_t create(const struct TimeModHostV1* host, void** instance) {
    struct State* s = calloc(1, sizeof(*s));
    *instance = s;
    if (!s) return 0;
    s->host = host;
    return host->find_region(host->context, "selection", &s->selection) &&
           host->find_region(host->context, "records", &s->records) &&
           host->find_region(host->context, "current", &s->current);
}
static void destroy(void* instance) { free(instance); }
static int32_t invoke(void* instance, struct TimeModCallV1* call) {
    struct State* s = instance;
    const struct TimeModHostV1* h = s->host;
    uint8_t id[2], value;
    uint32_t species;
    if (call->hook_id == 1) {
        if (call->argument >= 16 ||
            !h->read_region(h->context, s->selection, call->argument * 2, id, 2)) return 0;
        species = id[0] | ((uint32_t)id[1] << 8);
        if (!h->read_region(h->context, s->records, species * 29u, &value, 1) || !value) return 0;
        if (!h->write_region(h->context, s->current, 0, id, 2)) return 0;
        call->result = species;
        return 1;
    }
    if (call->hook_id != 2 || call->argument > 28 ||
        !h->read_region(h->context, s->current, 0, id, 2)) return 0;
    species = id[0] | ((uint32_t)id[1] << 8);
    if (species == 65535 && call->argument == 0) { call->result = 0; return 1; }
    if (!h->read_region(h->context, s->records, species * 29u + call->argument, &value, 1)) return 0;
    call->result = value;
    return 1;
}
static int32_t reset(void* instance) {
    struct State* s = instance;
    const uint8_t empty[2] = {255, 255};
    return s->host->write_region(s->host->context, s->current, 0, empty, 2);
}
static int32_t save(void* instance, uint8_t* bytes, uint32_t size) {
    (void)instance; (void)bytes; return size == 0;
}
static int32_t restore(void* instance, const uint8_t* bytes, uint32_t size) {
    (void)instance; (void)bytes; return size == 0;
}
static const struct TimeModApiV1 api = {
    sizeof(struct TimeModApiV1), TIME_MOD_ABI_V1, "pokered.title-species", "1", 1,
    create, destroy, invoke, reset, 0, save, restore
};
TIME_MOD_EXPORT const struct TimeModApiV1* time_get_mod_v1(void) { return &api; }
