#include "space/Session.hpp"
#include "machine/InputService.hpp"
#include "machine/plugins/PluginManager.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "fixtures/space/Fixture.hpp"
#include <iostream>
#include <thread>
#include <fstream>
#include <chrono>
#include <stdexcept>
using namespace BMMQ::Space;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"expected rejection");}
namespace {
class CapturedInput final : public BMMQ::IDigitalInputSourcePlugin {
public:
    BMMQ::InputPluginCapabilities capabilities() const noexcept override {return {.pollingSafe=true,.deterministic=true,.supportsDigital=true,.fixedLogicalLayout=true,.headlessSafe=true};}
    std::string_view name() const noexcept override {return "capture-input";}
    bool open() override {return true;}
    void close() noexcept override {}
    std::string_view lastError() const noexcept override {return {};}
    std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override {return 0x12;}
};
class CapturedPluginInput final : public BMMQ::IDigitalInputPlugin {
public:
    std::string_view id() const override {return "capture-plugin-input";}
    std::optional<uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override {return 0x34;}
};
}
int main(){
 try {
    auto rom=spaceFixture();auto hash=digest(rom);
    const auto dir=std::filesystem::temp_directory_path()/("space-smoke-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    // Both live input delivery routes and direct CLI input are recorded exactly once.
    for(bool adapter:{false,true}){
        GB::GameBoyMachine inputMachine;inputMachine.loadRom(rom);Session inputSession(inputMachine,rom);
        if(adapter){check(inputMachine.inputService().attachAdapter(std::make_unique<CapturedInput>()),"input attach failed");check(inputMachine.inputService().resume(),"input resume failed");}
        else {inputMachine.pluginManager().add(std::make_unique<CapturedPluginInput>());inputMachine.pluginManager().initialize(inputMachine.mutableView());}
        inputMachine.serviceInput();inputMachine.setJoypadState(0x56);
        auto inputs=inputSession.document()["inputs"];
        check(inputs.size()==2&&inputs[0]["mask"]==(adapter?0x12:0x34)&&inputs[1]["mask"]==0x56,"live input missing/duplicated");
        inputSession.checkpoint(dir/(adapter?"adapter-input":"plugin-input"));
        auto manifest=Project::read(dir/(adapter?"adapter-input/manifest.json":"plugin-input/manifest.json"));
        check(manifest["inputPosition"]=="2","checkpoint lost live input position");
    }
    // Internal mapped observation never emits CPU accesses, even during a CPU phase.
    {
        GB::GameBoyMachine observed;observed.loadRom(rom);Capture capture;observed.setAnalysisCapture(&capture);capture.phase=Kind::Read;
        auto& map=dynamic_cast<GB::GameBoyMemoryMap&>(observed.executionMemory().backingStore());
        auto fingerprint=observed.deterministicStateFingerprint();
        auto value=map.peek(0x150);check(map.peek(0xe000)==map.peek(0xc000),"peek lost echo mapping");
        (void)observed.videoStateSnapshot();(void)observed.stopSummary();(void)observed.runtimeContext().peek8(0x150);
        check(capture.pending()==0&&observed.deterministicStateFingerprint()==fingerprint,"inspection traced or changed state");
        check(map.read(0x150)==value&&capture.pending()==1,"CPU read no longer traced");
        observed.setAnalysisCapture(nullptr);
    }
    // CPU-intercepted spans apply acceptance to each individual address.
    {
        LR3592_DMG cpu;GB::GameBoyMemoryMap map;cpu.attachMemory(map);Capture capture;map.analysisCapture=&capture;capture.phase=Kind::Read;
        auto state=cpu.exportState();state.ppuDotCounter=80;cpu.importState(state);cpu.syncCachedIoRegisterWrite(0xff40,0x80);cpu.syncCachedIoRegisterWrite(0xff44,0);
        Record discard;while(capture.pop(discard)){};
        std::array<uint8_t,2> values{};
        check(cpu.handleMemoryRead(0x9fff,values),"VRAM span not intercepted");Record first,second;
        check(capture.pop(first)&&capture.pop(second)&&!first.accepted&&second.accepted,"VRAM span acceptance used base address");
        state=cpu.exportState();state.ppuDotCounter=0;cpu.importState(state);while(capture.pop(discard)){};
        check(cpu.handleMemoryRead(0xfe9f,values),"OAM span not intercepted");
        check(capture.pop(first)&&capture.pop(second)&&!first.accepted&&!second.accepted,"OAM/unusable span acceptance wrong");
        const uint8_t source=0xc0;cpu.handleMemoryWrite(0xff46,std::span(&source,1));while(capture.pop(discard)){}
        check(cpu.handleMemoryRead(0xff7f,values),"DMA span not intercepted");
        check(capture.pop(first)&&capture.pop(second)&&!first.accepted&&second.accepted,"DMA HRAM span acceptance used base address");
        map.analysisCapture=nullptr;
    }
    GB::GameBoyMachine baseline,traced;baseline.loadRom(rom);traced.loadRom(rom);
    auto loadedBytes=traced.cartridge().romBytes();
    Session session(traced,std::vector<uint8_t>(loadedBytes.begin(),loadedBytes.end()));
    for(int i=0;i<24;++i){baseline.step();traced.step();session.flush();}
    check(baseline.deterministicStateFingerprint()==traced.deterministicStateFingerprint(),"capture changed guest state");
    auto doc=session.document();check(doc["gaps"].empty(),"unexpected capture gap");check(doc["blocks"].size()<doc["instructions"].size(),"no multi-instruction blocks");
    bool readTwo=false,unchanged=false,call=false,ret=false;
    for(auto& d:doc["dependencies"]){if(d["location"]=="m:"+decimal(location(3,0,0xc000))){if(d["access"]=="read"&&d["value"]==2){readTwo=true;check(d["supplier"]["kind"]=="cpu","wrong supplier");}if(d["access"]=="write"&&d["value"]==3)unchanged=true;}}
    for(auto& e:doc["edges"]){call=call||e["kind"]=="call";ret=ret||e["kind"]=="return";}
    check(readTwo&&unchanged&&call&&ret,"missing execution evidence");
    // Actual DMA emits exactly one source read and destination write per byte.
    auto dmaRom=spaceFixture();dmaRom[0x150]=0x3e;dmaRom[0x151]=0xc0;dmaRom[0x152]=0xe0;dmaRom[0x153]=0x46;
    GB::GameBoyMachine dmaMachine,dmaBaseline;dmaMachine.loadRom(dmaRom);dmaBaseline.loadRom(dmaRom);Session dmaSession(dmaMachine,dmaRom);
    for(int i=0;i<3;++i){dmaMachine.step();dmaBaseline.step();dmaSession.flush();}
    auto dmaDoc=dmaSession.document();size_t dmaReads=0,dmaWrites=0;
    for(auto& d:dmaDoc["dependencies"])if(d["kind"]=="dma"){if(d["access"]=="read")++dmaReads;else ++dmaWrites;}
    check(dmaReads==160&&dmaWrites==160,"DMA access provenance missing/duplicated");
    check(dmaMachine.deterministicStateFingerprint()==dmaBaseline.deterministicStateFingerprint(),"DMA tracing changed state");
    // Interrupt entry and HALT polling are boundary events, not invented opcodes.
    auto interruptRom=spaceFixture();const uint8_t interruptProgram[]={0x3e,1,0xea,0xff,0xff,0xe0,0x0f,0xfb,0,0x76};
    std::copy(std::begin(interruptProgram),std::end(interruptProgram),interruptRom.begin()+0x150);interruptRom[0x40]=0xd9;
    GB::GameBoyMachine interruptMachine,interruptBaseline;interruptMachine.loadRom(interruptRom);interruptBaseline.loadRom(interruptRom);Session interruptSession(interruptMachine,interruptRom);
    for(int i=0;i<16;++i){interruptMachine.step();interruptBaseline.step();interruptSession.flush();}
    auto interruptDoc=interruptSession.document();bool interrupted=false,stalled=false;for(auto& event:interruptDoc["boundaries"]){interrupted=interrupted||event["kind"]=="interrupt";stalled=stalled||event["kind"]=="stall";}
    check(interrupted&&stalled,"interrupt/stall boundaries missing");check(interruptMachine.deterministicStateFingerprint()==interruptBaseline.deterministicStateFingerprint(),"interrupt capture changed state");
    // ROM banks at the same logical address produce distinct identities.
    Project banked(hash);auto emit=[&](uint64_t locationValue,uint16_t pc,uint16_t after,std::initializer_list<uint8_t> bytes){
        Record b;b.kind=Kind::Begin;b.location=locationValue;b.registers[5]=pc;banked.ingest(b);
        Record e=b;e.kind=Kind::End;e.registers[5]=after;e.length=bytes.size();std::copy(bytes.begin(),bytes.end(),e.bytes.begin());e.location=locationValue+bytes.size();banked.ingest(e);
    };
    emit(location(1,1,0),0x4000,0x4001,{0});emit(location(1,2,0),0x4000,0x4001,{0});
    emit(location(3,0,0xc000),0xc000,0xc001,{0});emit(location(3,0,0xc000),0xc000,0xc002,{0x3e,1});
    check(banked.document()["instructions"].size()==4,"bank/code revision identities collapsed");
    // An interior branch entry splits a previously captured straight-line block.
    Project split(hash);auto synthetic=[&](uint16_t pc,uint16_t next,std::initializer_list<uint8_t> bytes){
        Record b;b.kind=Kind::Begin;b.registers[5]=pc;b.location=location(1,0,pc);split.ingest(b);
        Record e=b;e.kind=Kind::End;e.length=bytes.size();e.registers[5]=next;e.location=location(1,0,next);std::copy(bytes.begin(),bytes.end(),e.bytes.begin());split.ingest(e);
    };
    synthetic(0x150,0x151,{0});synthetic(0x151,0x152,{0});synthetic(0x152,0x200,{0xc3,0,2});
    auto blocksBefore=split.document()["blocks"].size();synthetic(0x200,0x151,{0xc3,0x51,1});
    check(split.document()["blocks"].size()>blocksBefore,"interior target did not split block");
    synthetic(0x210,0x220,{0x20,0x0e});auto conditionalDoc=split.document();bool alternative=false,target=false;for(auto& edge:conditionalDoc["edges"]){alternative=alternative||edge["kind"]=="fallthrough";target=target||edge["kind"]=="target";}
    check(alternative&&target,"conditional alternatives missing");
    rejects([&]{traced.attachExecutorPolicy(BMMQ::Plugin::PortableIrStepPolicy{});});
    session.save(dir/"project.json");auto loaded=Project::load(dir/"project.json",hash);auto before=loaded.document();
    loaded.merge(doc);auto once=loaded.document();loaded.merge(doc);auto twice=loaded.document();
    check(once["dependencies"]==twice["dependencies"]&&once["edges"]==twice["edges"],"merge is not idempotent");
    rejects([&]{Project::load(dir/"project.json",std::string(64,'0'));});
    auto bad=doc;bad["edges"][0]["from"]="missing";rejects([&]{loaded.merge(bad);});
    check(loaded.document()["dependencies"]==twice["dependencies"],"bad merge partially mutated project");
    bad=doc;bad["instructions"].begin().value()["captures"]=Json::array();rejects([&]{loaded.merge(bad);});
    bad=doc;bad["history"]["views"].begin().value()["registers"]["A"]=256;rejects([&]{loaded.merge(bad);});
    bad=doc;bad["history"]["views"].begin().value()["captures"]["m:12884951040"]={{"value",-1},{"sequence","1"},{"branch","bad"}};rejects([&]{loaded.merge(bad);});
    bad=doc;bad["blocks"][0]["instructions"][0]="missing";rejects([&]{loaded.merge(bad);});
    bad=doc;bad["blocks"][0]["pools"]=Json::array({Json::array({"12884951040","99999"})});rejects([&]{loaded.merge(bad);});
    session.input(1);session.checkpoint(dir/"checkpoint");auto fingerprint=traced.deterministicStateFingerprint();auto savedHistory=session.document()["history"];
    for(int i=0;i<5;++i){traced.step();session.flush();}auto discoveries=session.document()["dependencies"].size();
    session.restore(dir/"checkpoint");check(traced.deterministicStateFingerprint()==fingerprint,"restore changed checkpoint state");
    auto restored=session.document();check(restored["history"]["writers"]==savedHistory["writers"],"restore retained abandoned writers");
    check(restored["dependencies"].size()>=discoveries,"restore discarded discoveries");
    auto manifest=Project::read(dir/"checkpoint/manifest.json");manifest["machineSha256"]=std::string(64,'0');Project::write(dir/"checkpoint/manifest.json",manifest);
    rejects([&]{session.restore(dir/"checkpoint");});check(traced.deterministicStateFingerprint()==fingerprint,"invalid restore mutated machine");
    exportHtml(loaded,dir/"graph.html");check(std::filesystem::file_size(dir/"graph.html")>1000,"no viewer");
    std::array<uint8_t,1> ld={0x77};auto metadata=decode(ld,0,1);check((metadata.reads&193)==193&&!(metadata.writes&192),"LD [HL],A operand metadata");
    std::array<uint8_t,1> xorA={0xaf};check(decode(xorA,0,1).writes&1,"unchanged A writes missing");
    std::array<uint8_t,1> popAf={0xf1};check((decode(popAf,0,1).writes&3)==3,"AF alias lanes missing");
    std::array<uint8_t,3> callNext={0xc4,3,0};check(decode(callNext,0,3,0).writes&256,"taken call to fallthrough lost SP write");check(!(decode(callNext,0,3,0x80).writes&256),"untaken call owns SP");
    // Producer/consumer handoff exercises the exact bounded queue under TSAN.
    Capture queue;std::atomic<bool> done{false};uint64_t consumed=0;
    std::thread consumer([&]{Record r;while(!done.load()||queue.pending()){if(queue.pop(r))++consumed;else std::this_thread::yield();}});
    for(int i=0;i<4000;++i){Record r;r.sequence=i;queue.push(r);}done.store(true);consumer.join();check(consumed==4000,"queue lost records");
    Capture overflow;for(size_t i=0;i<=Capture::capacity;++i)overflow.push({});check(overflow.lost()==1,"overflow not explicit");
    Project tiny(hash,1);Record begin;begin.kind=Kind::Begin;tiny.ingest(begin);Record end;end.kind=Kind::End;end.length=1;tiny.ingest(end);check(tiny.exhausted()&&!tiny.document()["gaps"].empty(),"budget exhaustion not visible");
    std::filesystem::remove_all(dir);std::cout<<"S.P.A.C.E. capture, persistence, history and handoff passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
