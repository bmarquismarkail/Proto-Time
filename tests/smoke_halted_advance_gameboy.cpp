#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/GameBoyMemoryMap.hpp"
#include "machine/plugins/debug/DebugEngine.hpp"
#include "inst_cycle/research/HaltedAdvance.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "WholeBlockFixtures.hpp"
#include "space/Capture.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <tuple>
#include <unistd.h>
using namespace BMMQ;
using namespace BMMQ::IR::Research;
namespace {
void require(bool v,const char* message) {if(!v)throw std::runtime_error(message);}
std::vector<uint8_t> rom(bool ei=false) {
  std::vector<uint8_t> r(32768);
  r[0x100]=ei?0xfb:0x76;r[0x101]=ei?0x76:0x06;r[0x102]=ei?0x06:0x42;r[0x103]=0x42;
  for(unsigned v:{0x40,0x48,0x50,0x58,0x60}){r[v]=0xf3;r[v+1]=0x76;}
  return r;
}
void prepare(GB::GameBoyMachine& m,unsigned mode=0,bool ei=false) {
  haltedAdvanceEnabled=false;m.loadRom(rom(ei));m.setIrComponents(nullptr,nullptr);
  if(mode==0){Plugin::DefaultStepPolicy p;m.attachExecutorPolicy(p);}
  if(mode==1){Plugin::VisibleStatePreservingStepPolicy p;m.attachExecutorPolicy(p);}
  if(mode==2){Plugin::PortableIrStepPolicy p;m.attachExecutorPolicy(p);}
  m.runtimeContext().write8(0xffff,0);m.runtimeContext().write8(0xff0f,0);
  const auto target=uint16_t(ei?0x102:0x101);
  for(unsigned n=0;m.readRegisterPair("PC")!=target && n<500;++n)m.step();
  require(m.readRegisterPair("PC")==target,"setup did not execute HALT");
}
void feedback(const CpuFeedback& a,const CpuFeedback& b,bool samePath=true){
  require(a.pcBefore==b.pcBefore && a.pcAfter==b.pcAfter && a.retiredCycles==b.retiredCycles &&
    a.isControlFlow==b.isControlFlow && a.segmentBoundaryHint==b.segmentBoundaryHint && (!samePath || a.executionPath==b.executionPath),"feedback mismatch");
}
void equivalent(GB::GameBoyMachine& a,GB::GameBoyMachine& b,bool samePath=true){
  require(a.deterministicStateFingerprint()==b.deterministicStateFingerprint(),"CPU/device/meta fingerprint mismatch");
  require(a.debugRegisters()==b.debugRegisters(),"register mismatch");
  require(a.recentAudioSamples()==b.recentAudioSamples() && a.audioFrameCounter()==b.audioFrameCounter(),"audio stream mismatch");
  feedback(a.runtimeContext().getLastFeedback(),b.runtimeContext().getLastFeedback(),samePath);
  const auto da=a.executionMemory().backingStore().readableSpan(0xfe00,160);
  const auto db=b.executionMemory().backingStore().readableSpan(0xfe00,160);
  require(std::equal(da.begin(),da.end(),db.begin()),"CPU DMA backing-state mismatch");
}
ExecutionSliceResult compare(GB::GameBoyMachine& a,GB::GameBoyMachine& b,ExecutionBudget budget,
    InstructionRetirementSink* oa=nullptr,InstructionRetirementSink* ob=nullptr){
  haltedAdvanceEnabled=false;const auto x=a.runSlice(budget,oa);
  haltedAdvanceEnabled=true;const auto y=b.runSlice(budget,ob);
  require(x.progress.retiredInstructions==y.progress.retiredInstructions && x.progress.retiredCycles==y.progress.retiredCycles && x.exitReason==y.exitReason,"progress/exit mismatch");
  feedback(x.lastFeedback,y.lastFeedback);equivalent(a,b);return y;
}
void write(GB::GameBoyMachine& a,GB::GameBoyMachine& b,uint16_t address,uint8_t value){
  // Arm CPU-owned hardware via its existing interceptor store. The attached map
  // path synchronizes registers but does not invoke the CPU's serial/DMA triggers.
  const bool cpuIo=address==0xff02 || address==0xff04 || address==0xff07 ||
      address==0xff41 || address==0xff44 || address==0xff46;
  for(auto* m:{&a,&b}) {
    auto& pool=m->executionMemory();
    (cpuIo ? pool.store : pool.backingStore()).write(std::span<const uint8_t>(&value,1),address);
  }
}
void windows(){
  std::mt19937 rng(20261011);
  for(unsigned mode=0;mode<3;++mode){
    GB::GameBoyMachine a,b;prepare(a,mode);prepare(b,mode);haltedAdvanceStats={};
    for(unsigned n=0;n<500;++n){
      compare(a,b,{rng()%513,rng()%4097,bool(rng()%2)});
      if(n%37==0){a.setJoypadState(uint8_t(n));b.setJoypadState(uint8_t(n));a.serviceInput();b.serviceInput();}
      if(n%41==0){write(a,b,0xff40,uint8_t(n%82==0?0:0x91));}
      if(n%43==0){ // Independently shifted CPU/device phases through the public context path.
        haltedAdvanceEnabled=false;a.runtimeContext().step();b.runtimeContext().step();
      }
    }
    require(haltedAdvanceStats.bulkCalls && haltedAdvanceStats.logicalIdlePolls>haltedAdvanceStats.bulkCalls*2,"no actual polling reduction");
    require(b.audioFrameCounter()>0,"audio chunks not exercised");
    const auto path=std::filesystem::temp_directory_path()/("time-halt-gb-"+std::to_string(getpid())+".ptstate");
    a.save_state(path);a.load_state(path);b.load_state(path);std::filesystem::remove(path);compare(a,b,{1000,4001,false});
    prepare(a,mode);prepare(b,mode);compare(a,b,{1000,4001,false});
  }
}
void timersSerialDma(){
  for(unsigned tac=0;tac<4;++tac){
    GB::GameBoyMachine a,b;prepare(a);prepare(b);
    write(a,b,0xff07,uint8_t(4|tac));write(a,b,0xff05,0xfc);write(a,b,0xff06,0x42);
    haltedAdvanceStats={};
    for(unsigned n=0;n<300;++n){compare(a,b,{127,509,false});
      if(n%11==0)write(a,b,0xff04,0); // DIV write falling edge.
      if(n%17==0)write(a,b,0xff07,uint8_t(4|(n%4))); // TAC edge.
      if(n%19==0)write(a,b,0xff05,0xff);
      if(n%23==0)write(a,b,0xff06,uint8_t(n));}
    require(haltedAdvanceStats.bulkCalls,"timer study never used bulk path");
  }
  GB::GameBoyMachine a,b;prepare(a);prepare(b);
  write(a,b,0xff01,0x53);write(a,b,0xff02,0x81);
  for(unsigned n=0;n<120;++n)compare(a,b,{19,77,false});
  require(a.runtimeContext().read8(0xff01)==0xff && (a.runtimeContext().read8(0xff0f)&8),"serial completion missing");
  for(unsigned i=0;i<160;++i){
    const auto value=uint8_t(i*7);
    // The current CPU DMA implementation reads/writes backingStore spans.
    for(auto* m:{&a,&b})m->executionMemory().backingStore().load(std::span<const uint8_t>(&value,1),uint16_t(0xc000+i));
  }
  write(a,b,0xff46,0xc0);haltedAdvanceStats={};
  compare(a,b,{100,400,false});require(!haltedAdvanceStats.bulkCalls,"DMA did not preserve byte cadence");
  for(unsigned n=0;n<100;++n)compare(a,b,{7,29,false});
  for(unsigned i=0;i<160;++i)require(a.executionMemory().backingStore().readableSpan(uint16_t(0xfe00+i),1)[0]==uint8_t(i*7),"CPU DMA backing bytes mismatch");
  std::vector<uint8_t> boot(256,0xaa);a.loadExternalBootRom(boot);b.loadExternalBootRom(boot);
  require(a.runtimeContext().read8(0)==0xaa,"external boot mapping absent");
  write(a,b,0xff50,1);compare(a,b,{100,400,false});
  write(a,b,0xff50,0);compare(a,b,{100,400,false});
  require(a.runtimeContext().read8(0)==0 && b.runtimeContext().read8(0)==0,"FF50 boot visibility was reversed");
}
void interrupts(){
  for(bool ei:{false,true})for(uint8_t irq:{uint8_t(1),uint8_t(2),uint8_t(4),uint8_t(8),uint8_t(16)}){
    GB::GameBoyMachine a,b;prepare(a,0,ei);prepare(b,0,ei);
    // EI;HALT has already cleared the delay via ordinary HALT retirement.
    write(a,b,0xffff,irq);write(a,b,0xff0f,irq);haltedAdvanceStats={};
    compare(a,b,{1,UINT64_MAX,false});
    require(!haltedAdvanceStats.bulkCalls,"pending interrupt bypassed exact wake/HALT bug decision");
    if(ei)require(a.readRegisterPair("PC")<0x100,"IME IRQ not accepted");
    else{require(a.readRegisterPair("BC")>>8==6 && a.readRegisterPair("PC")==0x102,"HALT bug duplicate operand missing");compare(a,b,{1,UINT64_MAX,false});}
  }
  for(unsigned kind=0;kind<3;++kind){
    GB::GameBoyMachine a,b;prepare(a,0,true);prepare(b,0,true);
    if(kind==0){write(a,b,0xff05,0xff);write(a,b,0xff06,0x22);write(a,b,0xff07,5);write(a,b,0xffff,4);}
    if(kind==1){write(a,b,0xff01,0x53);write(a,b,0xff02,0x81);write(a,b,0xffff,8);}
    if(kind==2){write(a,b,0xff41,0x68);write(a,b,0xffff,2);}
    bool woke=false;
    for(unsigned n=0;n<150 && !woke;++n){compare(a,b,{257,1025,false});woke=a.readRegisterPair("PC")<0x100;}
    require(woke,"timer/serial/STAT IRQ wake missing");
  }
}
struct Observer:InstructionRetirementSink{unsigned count=0;
 InstructionRetirementDecision retireInstruction(const CpuFeedback&,const ExecutionSliceProgress&)override{++count;return InstructionRetirementDecision::continueSlice();}};
struct Trace:IPlugin{std::vector<std::tuple<unsigned,uint64_t,uint16_t,uint8_t>> events;
 std::string_view id()const override{return "halt-gb-test";}
 void onMachineEvent(const MachineEvent& e,const MachineView&)override{events.emplace_back(unsigned(e.type),e.tick,e.address,e.value);}};
struct CustomPolicy:Plugin::IExecutorPolicyPlugin {
 Plugin::DefaultStepPolicy p;
 std::unique_ptr<Plugin::IExecutorPolicyPlugin> clone()const override{return std::make_unique<CustomPolicy>(*this);}
 const Plugin::PluginMetadata& metadata()const override{return p.metadata();}
 ExecutionGuarantee guarantee()const override{return p.guarantee();}
 ExecutionBackend backend()const override{return p.backend();}
 bool shouldRecord(const Plugin::FetchBlock& f,const CpuFeedback& c)const override{return p.shouldRecord(f,c);}
 bool shouldSegment(const Plugin::FetchBlock& f,const CpuFeedback& c)const override{return p.shouldSegment(f,c);}
};
struct CustomBackend:IR::IIrExecutionBackend {
 bool supports(uint32_t a,uint32_t v)const noexcept override{return a==GB::IRExecution::GameBoyCoreAdapter::kArchitectureId && v==IR::kIrAbiVersion;}
 BlockBackendArtifactPtr compile(const IR::BlockPtr&,std::string*)override{return {};}
 bool execute(const BlockBackendArtifact&,size_t,IR::InterpreterHost&,IR::InterpreterResult*)override{return false;}
};
void liveMutation(){
 GB::GameBoyMachine a,b;auto bytes=rom(true);
 // IRQ wake executes a guest write whose observer replaces the live policy.
 bytes[0x40]=0xf3;bytes[0x41]=0x3e;bytes[0x42]=1;
 bytes[0x43]=0xea;bytes[0x44]=0x00;bytes[0x45]=0xc0;bytes[0x46]=0x76;
 unsigned changed=0;CustomPolicy policy;
 for(auto* m:{&a,&b}){
   haltedAdvanceEnabled=false;m->loadRom(bytes);m->runtimeContext().write8(0xffff,0);m->runtimeContext().write8(0xff0f,0);
   for(unsigned n=0;m->readRegisterPair("PC")!=0x102 && n<500;++n)m->step();
   require(m->readRegisterPair("PC")==0x102,"mutation setup not halted");
   auto& map=dynamic_cast<GB::GameBoyMemoryMap&>(m->executionMemory().backingStore());
   const auto original=map.writeObserver();
   map.setWriteObserver([m,original,&policy,&changed](uint16_t address,uint8_t value){
     if(original)original(address,value);
     if(address==0xc000){m->attachExecutorPolicy(policy);++changed;}
   });
 }
 write(a,b,0xffff,1);write(a,b,0xff0f,1);haltedAdvanceStats={};
 compare(a,b,{128,UINT64_MAX,false});
 require(changed==2 && !haltedAdvanceStats.bulkCalls,"policy mutation was not rechecked before halted advance");
}
void emittedTransition(){
 const auto code=emittedGameBoyCompute();auto bytes=rom();bytes[0x200]=0x76;
 for(const auto& guard:code.block().guards)if(guard.kind==IR::GuardKind::CodeBytes)
   std::copy(guard.bytes.begin(),guard.bytes.end(),bytes.begin()+guard.subject);
 GB::GameBoyMachine a,b;
 for(auto* m:{&a,&b}){haltedAdvanceEnabled=false;m->loadRom(bytes);
   m->runtimeContext().writeRegister16("PC",uint16_t(code.block().guestStart));
   m->runtimeContext().writeRegister16("HL",0xc000);}
 auto binding=b.bindResearchBlock(code);const ExecutionBudget budget{code.block().instructions.size(),UINT64_MAX,false};
 haltedAdvanceEnabled=false;const auto x=a.runSlice(budget);
 haltedAdvanceEnabled=true;const auto y=b.runResearchBlock(binding,budget);
 require(x.progress.retiredInstructions==y.progress.retiredInstructions && x.progress.retiredCycles==y.progress.retiredCycles,"emitted active progress mismatch");
 // Native active execution deliberately reports NativeIr; halted feedback must match exactly.
 require(b.runtimeContext().getLastFeedback().executionPath==ExecutionPathHint::NativeIr,"emitted transition did not execute native IR");
 equivalent(a,b,false);
 for(auto* m:{&a,&b}){m->runtimeContext().writeRegister16("PC",0x200);m->step();
   require(m->readRegisterPair("PC")==0x201,"emitted transition did not halt");}
 haltedAdvanceStats={};compare(a,b,{1000,4001,false});require(haltedAdvanceStats.bulkCalls,"emitted fallback did not share Game Boy halted advance");
}
void fallback(){
 GB::GameBoyMachine a,b;prepare(a);prepare(b);Observer oa,ob;haltedAdvanceStats={};
 compare(a,b,{100,400,false},&oa,&ob);require(oa.count==100 && ob.count==100 && !haltedAdvanceStats.bulkCalls,"observer fallback missing");
 auto& ta=a.pluginManager().add(std::make_unique<Trace>());auto& tb=b.pluginManager().add(std::make_unique<Trace>());
 a.pluginManager().initialize(a.mutableView());b.pluginManager().initialize(b.mutableView());
 for(unsigned n=0;n<150;++n)compare(a,b,{100,400,false});
 require(ta.events==tb.events && !ta.events.empty() && !haltedAdvanceStats.bulkCalls,"plugin event fallback mismatch");
 GB::GameBoyMachine c,d;prepare(c);prepare(d);Space::Capture ca,cb;c.setAnalysisCapture(&ca);d.setAnalysisCapture(&cb);
 haltedAdvanceStats={};compare(c,d,{32,128,false});require(!haltedAdvanceStats.bulkCalls,"capture fallback missing");
 Space::Record x,y;while(ca.pop(x)){require(cb.pop(y) && x.kind==y.kind && x.address==y.address && x.value==y.value && x.cycles==y.cycles,"capture sequence mismatch");}
 require(!cb.pop(y),"extra capture record");c.setAnalysisCapture(nullptr);d.setAnalysisCapture(nullptr);
 GB::GameBoyMachine e,f;prepare(e);prepare(f);CustomPolicy policy;e.attachExecutorPolicy(policy);f.attachExecutorPolicy(policy);
 haltedAdvanceStats={};compare(e,f,{100,400,false});require(!haltedAdvanceStats.bulkCalls,"custom policy fallback missing");
 prepare(e,1);prepare(f,1);e.setIrComponents(nullptr,std::make_unique<CustomBackend>());f.setIrComponents(nullptr,std::make_unique<CustomBackend>());
 compare(e,f,{100,400,false});require(!haltedAdvanceStats.bulkCalls,"custom IR fallback missing");
 prepare(e);prepare(f);Space::Capture rawA,rawB;rawA.bindProducer();rawB.bindProducer();
 auto& ma=dynamic_cast<GB::GameBoyMemoryMap&>(e.executionMemory().backingStore());
 auto& mb=dynamic_cast<GB::GameBoyMemoryMap&>(f.executionMemory().backingStore());ma.analysisCapture=&rawA;mb.analysisCapture=&rawB;
 haltedAdvanceStats={};compare(e,f,{16,64,false});require(!haltedAdvanceStats.bulkCalls,"direct map capture fallback missing");
 unsigned records=0;while(rawA.pop(x)){require(rawB.pop(y) && x.kind==y.kind && x.address==y.address && x.value==y.value,"direct map capture mismatch");++records;}
 require(records>=48 && !rawB.pop(y),"per-poll map publications were skipped");ma.analysisCapture=nullptr;mb.analysisCapture=nullptr;
 Debug::DebugEngine ea,eb;e.connectDebugEngine(&ea);f.connectDebugEngine(&eb);ea.begin();eb.begin();
 haltedAdvanceStats={};compare(e,f,{1,4,false});require(!haltedAdvanceStats.bulkCalls,"debugger fallback missing");
 e.connectDebugEngine(nullptr);f.connectDebugEngine(nullptr);
}
}
int main()try{std::cerr<<"windows\n";windows();std::cerr<<"timers/serial/DMA\n";timersSerialDma();std::cerr<<"interrupts\n";interrupts();std::cerr<<"live mutation\n";liveMutation();std::cerr<<"emitted transition\n";emittedTransition();std::cerr<<"fallback\n";fallback();std::cout<<"Game Boy HALT budgets, clocks, IRQ/HALT bug, DMA/serial, audio/video and fallback parity passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
