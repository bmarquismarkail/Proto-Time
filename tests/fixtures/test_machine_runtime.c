#include "machine/plugins/abi/TimeMachineRuntimeAbi.h"
#include "machine/plugins/abi/TimeMachineProviderAbi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
static _Atomic unsigned releases, discards, legacy_calls;
struct State {unsigned pc;const char *mode;};
static void destroy(void* p) {free(p);atomic_fetch_add(&releases,1);}
static int32_t prepare(void* p,uint32_t op,const uint8_t* b,uint32_t n,void** t) {
    struct State* s=p;if(!t || !b || !n || op!=TIME_MACHINE_PREPARE_ROM) return 0;
    *t=malloc(1);if(!*t) return 0;
    return strcmp(s->mode,"prepare-fail")!=0;
}
static void commit(void* p,void* t) {((struct State*)p)->pc=0;free(t);}
static void discard(void* p,void* t) {(void)p;free(t);atomic_fetch_add(&discards,1);}
static int32_t step(void* p,struct TimeMachineRetirementV2* r,const struct TimeMachineEventSinkV2* sink) {
    struct State* s=p;if(!strcmp(s->mode,"step-fail")) return 0;
    r->pc_before=s->pc;r->pc_after=++s->pc;r->cycles=!strcmp(s->mode,"cycles")?0:4;
    if(!strcmp(s->mode,"overflow")) {
        struct TimeMachineEventV2 e={0};e.struct_size=sizeof(e);e.type=2;
        for(unsigned n=0;n<257;++n) (void)sink->emit(sink->context,&e);
    }
    return 1;
}
static int32_t read8(void* p,uint16_t a,uint32_t inspect,uint8_t* out) {(void)p;(void)a;(void)inspect;*out=0;return 1;}
static int32_t write8(void* p,uint16_t a,uint8_t b) {(void)p;(void)a;(void)b;return 1;}
static int32_t reg(void* p,const char* n,uint32_t w,uint16_t* out) {(void)n;(void)w;*out=((struct State*)p)->pc;return 1;}
static int32_t writeReg(void* p,const char* n,uint32_t w,uint16_t v) {(void)n;(void)w;((struct State*)p)->pc=v;return 1;}
static int32_t input(void* p,uint32_t m) {(void)p;return m<=255;}
static int32_t checkpoint(void* p,uint8_t* b,uint32_t cap,uint32_t* size) {
    struct State* s=p;*size=!strcmp(s->mode,"state-budget")?32u*1024u*1024u+1:1;
    if(b && cap) *b=0;
    return 1;
}
static int32_t fingerprint(void* p,char* b,uint32_t cap) {return snprintf(b,cap,"pc-%u",((struct State*)p)->pc)>0;}
static int32_t video(void* p,struct TimeMachineVideoV2* v,uint32_t* b,uint32_t cap) {
    (void)b;(void)cap;v->width=!strcmp(((struct State*)p)->mode,"video-bounds")?161:160;v->height=144;return 1;
}
static int32_t audio(void* p,struct TimeMachineAudioV2* a,int16_t* b,uint32_t cap) {
    (void)b;(void)cap;a->sample_rate=48000;a->channels=1;a->sample_count=!strcmp(((struct State*)p)->mode,"audio-bounds")?65537:0;return 1;
}
static int32_t create(struct TimeMachineRuntimeV2* out) {
    struct State* s=calloc(1,sizeof(*s));if(!s) return 0;
    s->mode=getenv("TIME_TEST_RUNTIME_MODE");if(!s->mode) s->mode="ok";
    *out=(struct TimeMachineRuntimeV2){sizeof(*out),2,s,destroy,prepare,commit,discard,step,read8,write8,reg,writeReg,input,checkpoint,fingerprint,video,audio};
    if(!strcmp(s->mode,"callback")) out->step=NULL;
    if(!strcmp(s->mode,"table-version")) out->abi_version=1;
    if(!strcmp(s->mode,"table-size")) out->struct_size=0;
    if(!strcmp(s->mode,"decline")) return 0;
    return 1;
}
unsigned time_test_runtime_releases(void) {return atomic_load(&releases);}
unsigned time_test_runtime_discards(void) {return atomic_load(&discards);}
unsigned time_test_runtime_legacy_calls(void) {return atomic_load(&legacy_calls);}
static void legacy_release(void* p) {(void)p;}
static int32_t legacy_create(const struct TimeMachineFactoryHostV1* host,struct TimeMachineFactoryResultV1* out) {
    uint64_t handle=0;
    if(!host->create_family(host->context,"gameboy",&handle)) return 0;
    *out=(struct TimeMachineFactoryResultV1){sizeof(*out),handle,NULL,legacy_release};return 1;
}
/* A valid old entry must never hide a rejected version-2 table. */
const struct TimeMachineProviderModuleV1* time_get_machine_provider_module_v1(void) {
    static const struct TimeMachineProviderV1 old={sizeof(old),1,"legacy-fallback","Legacy fallback","gameboy",160,144,legacy_create};
    static const struct TimeMachineProviderModuleV1 descriptor={sizeof(descriptor),1,1,"legacy.test",&old};
    atomic_fetch_add(&legacy_calls,1);return &descriptor;
}
static struct TimeMachineRuntimeProviderV2 provider;
static struct TimeMachineRuntimeModuleV2 module;
const struct TimeMachineRuntimeModuleV2* time_get_machine_runtime_module_v2(void) {
    const char* mode=getenv("TIME_TEST_RUNTIME_MODE");if(!mode) mode="ok";
    provider=(struct TimeMachineRuntimeProviderV2){sizeof(provider),2,"runtime-test","C ABI test double","gameboy",160,144,4194304,0,NULL,create};
    module=(struct TimeMachineRuntimeModuleV2){sizeof(module),2,1,"runtime.test.v2",&provider};
    if(!strcmp(mode,"module-version")) module.abi_version=1;
    if(!strcmp(mode,"family")) provider.family="third";
    if(!strcmp(mode,"region-count")) provider.region_count=65;
    if(!strcmp(mode,"clock")) provider.clock_hz=0;
    return &module;
}
