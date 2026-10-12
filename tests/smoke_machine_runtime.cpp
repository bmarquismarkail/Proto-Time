#include "emulator/DynamicMachineProvider.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/SaveState.hpp"
#include "space/CoreModel.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include <dlfcn.h>
#include <iostream>
#include <source_location>
#include <thread>
#include <unistd.h>

namespace {
void check(bool ok,std::source_location at=std::source_location::current()) {
    if(!ok) throw std::runtime_error(std::string(at.file_name())+":"+std::to_string(at.line())+" runtime provider check failed");
}
template<class F> void rejects(F&& f) {bool rejected=false;try {f();}catch(const std::exception&) {rejected=true;}check(rejected);}
struct Event {
    BMMQ::MachineEventType type; BMMQ::PluginCategory category;
    std::uint64_t tick;std::uint16_t address;std::uint8_t value;bool feedback;std::string detail;
    std::array<std::uint32_t,6> cpu{};
    bool operator==(const Event&) const = default;
};
struct Recorder final : BMMQ::IDigitalInputPlugin {
    std::vector<Event> events;std::uint32_t mask=0;
    std::string_view id() const override {return "runtime.differential.observer";}
    std::optional<std::uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override {return mask;}
    void onMachineEvent(const BMMQ::MachineEvent& e,const BMMQ::MachineView&) override {
        events.push_back({e.type,e.category,e.tick,e.address,e.value,e.feedback!=nullptr,std::string(e.detail)});
        if(e.feedback) {const auto& f=*e.feedback;events.back().cpu={f.pcBefore,f.pcAfter,f.retiredCycles,f.segmentBoundaryHint,f.isControlFlow,static_cast<std::uint32_t>(f.executionPath)};}
    }
};
struct Observer final : BMMQ::InstructionRetirementSink {
    std::uint64_t count=0;
    BMMQ::InstructionRetirementDecision retireInstruction(const BMMQ::CpuFeedback&,const BMMQ::ExecutionSliceProgress&) override {
        ++count;return BMMQ::InstructionRetirementDecision::exitSlice(BMMQ::ExecutionSliceExitReason::RetirementRequested);
    }
};
std::vector<std::uint8_t> rom(bool gg) {
    std::vector<std::uint8_t> bytes(32768);
    if(gg) {const std::uint8_t code[]={0xdd,0x21,0,0xc0,0x3e,0x34,0xdd,0x77,0,8,8,0xd3,0x7f,0x3c,0xc3,6,0};std::copy(std::begin(code),std::end(code),bytes.begin());}
    else {const std::uint8_t code[]={0x3e,0x12,0xea,0,0xc0,0x3c,0x18,0xfa};std::copy(std::begin(code),std::end(code),bytes.begin()+0x100);}
    return bytes;
}
void compare(BMMQ::Machine& baseline,BMMQ::Machine& external) {
    check(baseline.deterministicStateFingerprint()==external.deterministicStateFingerprint());
    for(const auto* r:{"PC","SP","AF","BC","DE","HL"}) check(baseline.readRegisterPair(r)==external.readRegisterPair(r));
    check(baseline.runtimeContext().peek8(0xc000)==external.runtimeContext().peek8(0xc000));
    check(baseline.currentDigitalInputMask().value_or(0)==external.currentDigitalInputMask().value_or(0));
}
void positive(const char* module,bool gg) {
    std::unique_ptr<BMMQ::Machine> external;
    {BMMQ::MachineRegistry registry;BMMQ::registerDynamicMachineProviders(registry,module);external=registry.create(gg?"runtime-gg":"runtime-gb");}
    check(dynamic_cast<GB::GameBoyMachine*>(external.get())==nullptr);
    check(dynamic_cast<BMMQ::GameGearMachine*>(external.get())==nullptr);
    std::unique_ptr<BMMQ::Machine> baseline;
    if(gg) baseline=std::make_unique<BMMQ::GameGearMachine>();else baseline=std::make_unique<GB::GameBoyMachine>();
    auto& b=baseline->pluginManager().add(std::make_unique<Recorder>());
    auto& e=external->pluginManager().add(std::make_unique<Recorder>());
    auto bytes=rom(gg);baseline->loadRom(bytes);external->loadRom(bytes);
    auto& nativeDebug=dynamic_cast<BMMQ::Debug::IDebugMachineV1&>(*baseline);
    const auto& model=BMMQ::Space::coreModel(gg?"gamegear":"gameboy");
    for(std::size_t n=0;n<model.pairs.size();++n)
        check(external->runtimeContext().readRegister16(model.pairs[n])==nativeDebug.debugRegisters()[n]);
    if(gg) {
        auto registers=nativeDebug.debugRegisters();registers[8]=0x7654;registers[12]=0x23;
        nativeDebug.debugCommitRegisters(registers);
        external->runtimeContext().writeRegister16("AF'",0x7654);external->runtimeContext().writeRegister8("I",0x23);
        check(external->runtimeContext().readRegister8("A'")==0x76);
        rejects([&]{external->runtimeContext().writeRegister8("IM",3);});
        check(external->runtimeContext().readRegister16("IM")==registers[17]);
    }
    baseline->pluginManager().initialize(baseline->mutableView());external->pluginManager().initialize(external->mutableView());
    check(external->describeIoRegions().size()==baseline->describeIoRegions().size());
    for(std::size_t n=0;n<baseline->describeIoRegions().size();++n) {
        const auto a=baseline->describeIoRegions()[n],c=external->describeIoRegions()[n];
        check(a.category==c.category && a.start==c.start && a.size==c.size && a.label==c.label && a.readable==c.readable && a.writable==c.writable);
    }
    for(unsigned n=0;n<1000;++n) {
        b.events.clear();e.events.clear();b.mask=(n/37)&0xff;
        external->inputService().publishDigitalSnapshot(b.mask,external->observationGeneration());baseline->serviceInput();
        const auto a=baseline->runSlice(BMMQ::ExecutionBudget{}),c=external->runSlice(BMMQ::ExecutionBudget{});
        check(a.lastFeedback.pcBefore==c.lastFeedback.pcBefore && a.lastFeedback.pcAfter==c.lastFeedback.pcAfter &&
            a.lastFeedback.retiredCycles==c.lastFeedback.retiredCycles && a.lastFeedback.segmentBoundaryHint==c.lastFeedback.segmentBoundaryHint &&
            a.lastFeedback.isControlFlow==c.lastFeedback.isControlFlow && a.lastFeedback.executionPath==c.lastFeedback.executionPath);
        compare(*baseline,*external);check(b.events==e.events);
    }
    if(!gg) {
        for(auto* m:{baseline.get(),external.get()}) {m->runtimeContext().write8(0xffff,1);m->runtimeContext().write8(0xff0f,1);}
        baseline->serviceInput();
        const auto a=baseline->runSlice({50,100000,false}),c=external->runSlice({50,100000,false});
        check(a.exitReason==BMMQ::ExecutionSliceExitReason::MachineBoundary && c.exitReason==a.exitReason);
        check(a.progress.retiredInstructions==1 && c.progress.retiredInstructions==1);compare(*baseline,*external);
        for(auto* m:{baseline.get(),external.get()}) {m->runtimeContext().write8(0xffff,0);m->runtimeContext().write8(0xff0f,0);}
    }
    auto video=external->realtimeVideoPacket({160,144});auto reference=baseline->realtimeVideoPacket({160,144});
    check(video.has_value() && reference.has_value());std::vector<std::uint32_t> a,c;
    check(BMMQ::decodeVideoSurface(video->packet.surface,160,144,a));check(BMMQ::decodeVideoSurface(reference->packet.surface,160,144,c));check(a==c);
    check(external->recentAudioSamples()==baseline->recentAudioSamples());check(external->audioFrameCounter()==baseline->audioFrameCounter());
    const auto copy=a;external->step();check(video->packet.surface.argbPixels==copy);
    const auto path=std::filesystem::temp_directory_path()/("runtime-provider-"+std::to_string(getpid())+(gg?"-gg":"-gb")+".ptime");
    const auto region=external->modHost().createRegion("provider-region",16);external->modHost().region(region)[3]=91;
    external->save_state(path);const auto fingerprint=external->deterministicStateFingerprint();
    external->modHost().region(region)[3]=90;check(external->deterministicStateFingerprint()!=fingerprint);
    external->modHost().region(region)[3]=91;
    auto wrongCore=BMMQ::createProvidedMachine(gg?"runtime-gb":"runtime-gg",std::filesystem::path(module));
    wrongCore.machine->loadRom(rom(!gg));const auto wrongFingerprint=wrongCore.machine->deterministicStateFingerprint();
    rejects([&]{wrongCore.machine->load_state(path);});check(wrongCore.machine->deterministicStateFingerprint()==wrongFingerprint);
    const auto mask=external->currentDigitalInputMask();external->step();external->runtimeContext().write8(0xc000,199);external->modHost().region(region)[3]=1;
    external->load_state(path);check(external->deterministicStateFingerprint()==fingerprint);check(external->modHost().region(region)[3]==91);check(external->currentDigitalInputMask()==mask);
    auto state=BMMQ::SaveStateReader::read(path);
    for(auto& chunk:state.chunks) if(chunk.name=="foreign.state") chunk.data[0]^=0xff;
    BMMQ::SaveStateReader::write(state,path);rejects([&]{external->load_state(path);});check(external->deterministicStateFingerprint()==fingerprint);check(external->modHost().region(region)[3]==91);
    external->save_state(path);
    bytes[0x150]^=1;external->loadRom(bytes);const auto other=external->deterministicStateFingerprint();rejects([&]{external->load_state(path);});check(external->deterministicStateFingerprint()==other);
    external->modHost().region(region)[3]=11;external->loadRom(bytes);check(external->modHost().region(region)[3]==0);check(external->currentDigitalInputMask()==0);
    Observer observer;const auto result=external->runSlice({50,100000,false},&observer);check(result.progress.retiredInstructions==1 && observer.count==1);
    check(external->runSlice({0,100,false}).progress.retiredInstructions==0);check(external->runSlice({10,0,false}).progress.retiredInstructions==0);
    rejects([&]{external->attachExecutorPolicy(BMMQ::Plugin::PortableIrStepPolicy{});});
    rejects([&]{external->realtimeVideoPacket({320,288});});
    std::filesystem::remove(path);
    if(gg) {
        auto irqRom=rom(true);
        const std::uint8_t code[]={0x3e,0x20,0xd3,0xbf,0x3e,0x81,0xd3,0xbf,0xc3,8,0};
        std::copy(std::begin(code),std::end(code),irqRom.begin());
        baseline->loadRom(irqRom);external->loadRom(irqRom);b.mask=0;
        baseline->serviceInput();
        const auto a=baseline->runSlice({20000,1000000,false}),c=external->runSlice({20000,1000000,false});
        check(a.exitReason==BMMQ::ExecutionSliceExitReason::MachineBoundary && c.exitReason==a.exitReason);
        check(a.progress.retiredInstructions==c.progress.retiredInstructions && a.progress.retiredCycles==c.progress.retiredCycles);
        // Reset the host-only region before comparing ordinary core state.
        check(baseline->readRegisterPair("PC")==external->readRegisterPair("PC"));
        check(baseline->recentAudioSamples()==external->recentAudioSamples());
    }
    // Construction BIOS and one-way Game Boy FF50 still execute module semantics.
    auto boot=BMMQ::createProvidedMachine(gg?"runtime-gg":"runtime-gb",std::filesystem::path(module));
    std::vector<std::uint8_t> bios(256);bios[0]=0x3e;bios[1]=0x42;
    dynamic_cast<BMMQ::IExternalBootRomMachine&>(*boot.machine).loadExternalBootRom(bios);boot.machine->loadRom(rom(gg));boot.machine->step();check((boot.machine->readRegisterPair("AF")>>8)==0x42);
    if(!gg) {boot.machine->runtimeContext().write8(0xff50,1);boot.machine->runtimeContext().write8(0xff50,0);check(boot.machine->runtimeContext().peek8(0)==0);}
    rejects([&]{dynamic_cast<BMMQ::IExternalBootRomMachine&>(*boot.machine).loadExternalBootRom(bios);});
}
}
int main(int argc,char** argv) {
    check(argc==2);positive(argv[1],false);positive(argv[1],true);
    std::array<std::jthread,4> threads;
    for(unsigned n=0;n<threads.size();++n) threads[n]=std::jthread([&,n]{auto i=BMMQ::createProvidedMachine(n%2?"runtime-gg":"runtime-gb",std::filesystem::path(argv[1]));i.machine->loadRom(rom(n%2));i.machine->step();});
    for(auto& t:threads)t.join();
    std::cout<<"Module-owned both-core retirement, events, state, input, audio/video, BIOS and lifetime passed\n";
}
