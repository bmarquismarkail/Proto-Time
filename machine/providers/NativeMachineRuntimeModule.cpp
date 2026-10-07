// Reference runtime module: both complete existing cores live inside this DSO.
// Only the separately versioned C table is visible to the host adapter.
#include "machine/plugins/abi/TimeMachineRuntimeAbi.h"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/CoreModel.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <unistd.h>

namespace {
constexpr std::uint32_t Budget=32u*1024u*1024u;
struct TempFile {
    std::filesystem::path directory,path;
    TempFile() {
        std::string name=(std::filesystem::temp_directory_path()/"time-provider-XXXXXX").string();
        std::vector<char> writable(name.begin(),name.end());writable.push_back(0);
        const auto* made=mkdtemp(writable.data());if(!made) throw std::runtime_error("provider temporary directory unavailable");
        directory=made;path=directory/"state.ptime";
    }
    ~TempFile() { std::error_code ignored;std::filesystem::remove_all(directory,ignored); }
};
struct Instance;
struct EventBridge final : BMMQ::IDigitalInputPlugin {
    Instance& owner;
    explicit EventBridge(Instance& i):owner(i) {}
    std::string_view id() const override { return "time.runtime.bridge"; }
    std::optional<std::uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override;
    void onMachineEvent(const BMMQ::MachineEvent&,const BMMQ::MachineView&) override;
};
struct Transaction {
    Instance* owner;
    std::unique_ptr<BMMQ::Machine> machine;
    std::vector<std::uint8_t> rom,bios;
    std::uint32_t input=0;
};
struct Instance {
    bool gameGear;
    std::unique_ptr<BMMQ::Machine> machine;
    std::vector<std::uint8_t> rom,bios;
    std::uint32_t mask=0;
    const TimeMachineEventSinkV2* sink=nullptr;
    bool eventFailure=false;
    bool inputPending=true;
    std::unordered_set<Transaction*> pending;
    std::unique_ptr<Transaction> retired;
    explicit Instance(bool gg):gameGear(gg) { machine=makeCore(); }
    ~Instance() {
        for(auto* t:pending) delete t;
        if(machine) { try { machine->pluginManager().shutdown(machine->mutableView()); } catch (...) {} }
    }
    std::unique_ptr<BMMQ::Machine> makeCore() {
        std::unique_ptr<BMMQ::Machine> core;
        if(gameGear) core=std::make_unique<BMMQ::GameGearMachine>();else core=std::make_unique<GB::GameBoyMachine>();
        core->pluginManager().add(std::make_unique<EventBridge>(*this));
        core->pluginManager().initialize(core->mutableView());
        return core;
    }
    void configure(BMMQ::Machine& core,const std::vector<std::uint8_t>& r,const std::vector<std::uint8_t>& b) {
        if(!b.empty()) {
            dynamic_cast<BMMQ::IExternalBootRomMachine&>(core).loadExternalBootRom(b);
            core.setProviderBootRom(b);
        }
        if(!r.empty()) core.loadRom(r);
    }
};
std::optional<std::uint32_t> EventBridge::sampleDigitalInput(const BMMQ::MachineView&) { return owner.mask; }
void EventBridge::onMachineEvent(const BMMQ::MachineEvent& event,const BMMQ::MachineView&) {
    if(!owner.sink) return;
    TimeMachineEventV2 copy{};copy.struct_size=sizeof(copy);copy.type=static_cast<std::uint32_t>(event.type);
    copy.category=static_cast<std::uint32_t>(event.category);copy.step=event.tick;copy.address=event.address;copy.value=event.value;
    copy.has_feedback=event.feedback!=nullptr;
    if(event.feedback) {
        const auto& f=*event.feedback;
        copy.feedback={sizeof(copy.feedback),f.pcBefore,f.pcAfter,f.retiredCycles,f.segmentBoundaryHint,f.isControlFlow,static_cast<std::uint32_t>(f.executionPath),0};
    }
    std::memcpy(copy.message,event.detail.data(),std::min(event.detail.size(),sizeof(copy.message)-1));
    if(owner.sink->emit(owner.sink->context,&copy)!=1) owner.eventFailure=true;
}
template<class F> int32_t safe(F&& fn) noexcept { try { fn();return 1; } catch (...) {return 0;} }
void destroy(void* p) noexcept { delete static_cast<Instance*>(p); }
int32_t prepare(void* p,uint32_t op,const uint8_t* bytes,uint32_t size,void** result) noexcept {
    if(result) *result=nullptr;
    return safe([&] {
        if(!p || !result || !bytes || !size || size>Budget) throw std::invalid_argument("runtime transition arguments");
        auto& i=*static_cast<Instance*>(p);auto t=std::make_unique<Transaction>();t->owner=&i;t->rom=i.rom;t->bios=i.bios;t->input=i.mask;
        if(!i.pending.empty()) throw std::invalid_argument("one pending transition per runtime instance");
        // Destroy the previous machine on preparation, outside publication.
        i.retired.reset();
        if(op==TIME_MACHINE_PREPARE_ROM) {t->rom.assign(bytes,bytes+size);t->input=0;}
        else if(op==TIME_MACHINE_PREPARE_BIOS) {if(!i.rom.empty() || size>32768) throw std::invalid_argument("BIOS construction only");t->bios.assign(bytes,bytes+size);}
        else if(op!=TIME_MACHINE_PREPARE_STATE || i.rom.empty()) throw std::invalid_argument("runtime transition unsupported");
        t->machine=i.makeCore();i.configure(*t->machine,t->rom,t->bios);
        if(op==TIME_MACHINE_PREPARE_STATE) {
            TempFile file;std::ofstream out(file.path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes),size);out.close();
            if(!out) throw std::runtime_error("provider state staging write failed");
            t->machine->load_state(file.path);t->input=t->machine->currentDigitalInputMask().value_or(0);
        }
        auto* token=t.get();i.pending.insert(token);*result=t.release();
    });
}
void discard(void* p,void* token) noexcept {
    auto& i=*static_cast<Instance*>(p);auto* t=static_cast<Transaction*>(token);
    if(i.pending.erase(t)) delete t;
}
void commit(void* p,void* token) noexcept {
    auto& i=*static_cast<Instance*>(p);auto* t=static_cast<Transaction*>(token);
    if(!i.pending.erase(t)) std::terminate();
    // All allocation/validation happened in prepare; publication only swaps.
    i.machine.swap(t->machine);i.rom.swap(t->rom);i.bios.swap(t->bios);i.mask=t->input;
    i.inputPending=true;
    // prepare guarantees no prior retired token; no destructor work here.
    if(i.retired) std::terminate();
    i.retired.reset(t);
}
int32_t step(void* p,TimeMachineRetirementV2* out,const TimeMachineEventSinkV2* sink) noexcept {
    return safe([&] {
        if(!out || out->struct_size!=sizeof(*out) || !sink || sink->struct_size!=sizeof(*sink) || !sink->emit) throw std::invalid_argument("retirement arguments");
        auto& i=*static_cast<Instance*>(p);if(i.rom.empty()) throw std::invalid_argument("retire before ROM");
        i.sink=sink;i.eventFailure=false;
        struct Scope {Instance& i;~Scope(){i.sink=nullptr;}} scope{i};
        if(i.inputPending) { i.machine->serviceInput();i.inputPending=false; }
        const auto result=i.machine->runSlice(BMMQ::ExecutionBudget{});
        if(result.progress.retiredInstructions!=1 || i.eventFailure) throw std::runtime_error("retirement/event handoff failed");
        const auto& f=result.lastFeedback;
        *out={sizeof(*out),f.pcBefore,f.pcAfter,f.retiredCycles,f.segmentBoundaryHint,f.isControlFlow,static_cast<std::uint32_t>(f.executionPath),result.exitReason==BMMQ::ExecutionSliceExitReason::MachineBoundary};
    });
}
int32_t read8(void* p,uint16_t a,uint32_t inspection,uint8_t* out) noexcept {
    return safe([&] {if(!out || inspection>1) throw std::invalid_argument("read arguments");auto& r=static_cast<Instance*>(p)->machine->runtimeContext();*out=inspection?r.peek8(a):r.read8(a);});
}
int32_t write8(void* p,uint16_t a,uint8_t value) noexcept {return safe([&]{static_cast<Instance*>(p)->machine->runtimeContext().write8(a,value);});}
int32_t readRegister(void* p,const char* name,uint32_t width,uint16_t* out) noexcept {
    return safe([&]{
        if(!name || !out || !strnlen(name,33) || strnlen(name,33)>32 || (width!=8 && width!=16)) throw std::invalid_argument("register arguments");
        auto& i=*static_cast<Instance*>(p);const auto& model=BMMQ::Space::coreModel(i.gameGear?"gamegear":"gameboy");
        auto& debug=dynamic_cast<BMMQ::Debug::IDebugMachineV1&>(*i.machine);const auto values=debug.debugRegisters();
        if(width==16) for(std::size_t n=0;n<model.pairs.size();++n) if(std::string_view(name)==model.pairs[n]) {*out=values[n];return;}
        if(width==8) for(const auto& lane:model.lanes) if(lane.byte && std::string_view(name)==lane.name) {
            *out=lane.high?values[lane.pair]>>8:values[lane.pair]&255;return;
        }
        throw std::invalid_argument("register name/width unsupported");
    });
}
int32_t writeRegister(void* p,const char* name,uint32_t width,uint16_t value) noexcept {
    return safe([&]{
        if(!name || !strnlen(name,33) || strnlen(name,33)>32 || (width!=8 && width!=16) || (width==8 && value>255)) throw std::invalid_argument("register arguments");
        auto& i=*static_cast<Instance*>(p);const auto& model=BMMQ::Space::coreModel(i.gameGear?"gamegear":"gameboy");
        auto& debug=dynamic_cast<BMMQ::Debug::IDebugMachineV1&>(*i.machine);auto values=debug.debugRegisters();bool found=false;
        if(width==16) for(std::size_t n=0;n<model.pairs.size();++n) if(std::string_view(name)==model.pairs[n]) {values[n]=value;found=true;break;}
        if(width==8) for(const auto& lane:model.lanes) if(lane.byte && std::string_view(name)==lane.name) {
            values[lane.pair]=lane.high?static_cast<uint16_t>((values[lane.pair]&255)|(value<<8)):static_cast<uint16_t>((values[lane.pair]&0xff00)|value);found=true;break;
        }
        if(!found || !debug.debugValidateRegisters(values)) throw std::invalid_argument("register value/name rejected");
        debug.debugCommitRegisters(values);
    });
}
int32_t input(void* p,uint32_t mask) noexcept {
    if(mask>255) return 0;
    auto& i=*static_cast<Instance*>(p);i.mask=mask;i.inputPending=true;return 1;
}
int32_t checkpoint(void* p,uint8_t* bytes,uint32_t capacity,uint32_t* size) noexcept {
    return safe([&] {
        if(!size || (!bytes && capacity)) throw std::invalid_argument("checkpoint arguments");
        auto& i=*static_cast<Instance*>(p);TempFile file;i.machine->save_state(file.path);
        const auto length=std::filesystem::file_size(file.path);if(!length || length>Budget) throw std::runtime_error("checkpoint budget");
        *size=length;if(!bytes && !capacity) return;
        if(capacity<length) throw std::invalid_argument("checkpoint capacity");
        std::ifstream in(file.path,std::ios::binary);in.read(reinterpret_cast<char*>(bytes),length);if(!in) throw std::runtime_error("checkpoint copy failed");
    });
}
int32_t fingerprint(void* p,char* bytes,uint32_t capacity) noexcept {
    return safe([&]{const auto value=static_cast<Instance*>(p)->machine->deterministicStateFingerprint();if(!bytes || capacity<=value.size()) throw std::invalid_argument("fingerprint capacity");std::memcpy(bytes,value.c_str(),value.size()+1);});
}
int32_t video(void* p,TimeMachineVideoV2* meta,uint32_t* pixels,uint32_t capacity) noexcept {
    return safe([&] {
        if(!meta || meta->struct_size!=sizeof(*meta) || !pixels || capacity<160u*144u) throw std::invalid_argument("video arguments");
        const auto submission=static_cast<Instance*>(p)->machine->realtimeVideoPacket({160,144});
        if(!submission || submission->packet.empty()) throw std::runtime_error("video unavailable");
        const auto& frame=submission->packet;std::vector<std::uint32_t> owned;
        if(!BMMQ::decodeVideoSurface(frame.surface,frame.width,frame.height,owned) || owned.size()!=160u*144u) throw std::runtime_error("video decode failed");
        std::copy(owned.begin(),owned.end(),pixels);
        *meta={sizeof(*meta),160,144,frame.displayEnabled,frame.inVBlank,frame.scanlineIndex.value_or(UINT16_MAX)};
        if(!frame.scanlineIndex) meta->scanline=UINT32_MAX;
    });
}
int32_t audio(void* p,TimeMachineAudioV2* meta,int16_t* samples,uint32_t capacity) noexcept {
    return safe([&] {
        if(!meta || meta->struct_size!=sizeof(*meta) || (!samples && capacity)) throw std::invalid_argument("audio arguments");
        const auto packet=static_cast<Instance*>(p)->machine->realtimeAudioPacket();
        if(!packet || packet->pcmSamples.size()>65536) throw std::runtime_error("audio unavailable/budget");
        *meta={sizeof(*meta),packet->sampleRate,packet->channelCount,static_cast<std::uint32_t>(packet->pcmSamples.size()),packet->frameCounter};
        if(!samples && !capacity) return;
        if(capacity<packet->pcmSamples.size()) throw std::invalid_argument("audio capacity");
        std::copy(packet->pcmSamples.begin(),packet->pcmSamples.end(),samples);
    });
}
int32_t create(TimeMachineRuntimeV2* out,bool gg) noexcept {
    return safe([&] {
        if(!out || out->struct_size!=sizeof(*out) || out->abi_version!=TIME_MACHINE_RUNTIME_ABI_V2) throw std::invalid_argument("runtime factory ABI");
        auto owned=std::make_unique<Instance>(gg);
        *out={sizeof(*out),TIME_MACHINE_RUNTIME_ABI_V2,owned.release(),destroy,prepare,commit,discard,step,read8,write8,readRegister,writeRegister,input,checkpoint,fingerprint,video,audio};
    });
}
int32_t createGb(TimeMachineRuntimeV2* out) noexcept {return create(out,false);}
int32_t createGg(TimeMachineRuntimeV2* out) noexcept {return create(out,true);}
const TimeMachineRegionV2 gbRegions[]={
    {sizeof(TimeMachineRegionV2),1,1,1,0x8000,0x2000,"VRAM"},
    {sizeof(TimeMachineRegionV2),1,1,1,0xfe00,0xa0,"OAM"},
    {sizeof(TimeMachineRegionV2),1,1,1,0xff40,0xc,"LCD Registers"},
    {sizeof(TimeMachineRegionV2),2,1,1,0xff10,0x17,"APU Registers"},
    {sizeof(TimeMachineRegionV2),2,1,1,0xff30,0x10,"Wave RAM"},
    {sizeof(TimeMachineRegionV2),3,1,1,0xff00,1,"Joypad"},
    {sizeof(TimeMachineRegionV2),6,1,1,0xff01,2,"Serial Registers"}
};
const TimeMachineRegionV2 ggRegions[]={
    {sizeof(TimeMachineRegionV2),0,1,0,0,0x8000,"ROM"},
    {sizeof(TimeMachineRegionV2),0,1,1,0xc000,0x2000,"RAM"},
    {sizeof(TimeMachineRegionV2),2,1,1,0xff10,0x17,"APU Registers"},
    {sizeof(TimeMachineRegionV2),1,1,1,0xbe,2,"VDP Ports (IO)"},
    {sizeof(TimeMachineRegionV2),3,1,0,0xdc,2,"Input Ports (0xDC/0xDD)"}
};
const TimeMachineRuntimeProviderV2 providers[]={
    {sizeof(TimeMachineRuntimeProviderV2),2,"runtime-gb","Module-owned Game Boy","gameboy",160,144,4194304,7,gbRegions,createGb},
    {sizeof(TimeMachineRuntimeProviderV2),2,"runtime-gg","Module-owned Game Gear","gamegear",160,144,3579545,5,ggRegions,createGg}
};
const TimeMachineRuntimeModuleV2 module={sizeof(module),2,2,"time.native.runtime.v2",providers};
}
extern "C" const TimeMachineRuntimeModuleV2* time_get_machine_runtime_module_v2() noexcept {return &module;}
