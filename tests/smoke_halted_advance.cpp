#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearPSG.hpp"
#include "cores/gamegear/GameGearVDP.hpp"
#include "cores/gamegear/GameGearIrExecution.hpp"
#include "inst_cycle/research/HaltedAdvance.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "space/Capture.hpp"
#include "WholeBlockFixtures.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <tuple>
#include <unistd.h>
using namespace BMMQ;
using namespace BMMQ::IR::Research;
namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
std::vector<uint8_t> rom(bool irq) {
  std::vector<uint8_t> r(32768);
  const uint8_t program[] = {0x3e, uint8_t(irq ? 0x60 : 0x40), 0xd3, 0xbf,
      0x3e, 0x81, 0xd3, 0xbf, 0x3e, 0x90, 0xd3, 0x7f, 0xfb, 0x00, 0x76};
  std::copy(std::begin(program), std::end(program), r.begin()+0x100);
  r[0x38]=0xf3; r[0x39]=0x76; // DI; HALT, with the original pending-IRQ semantics.
  r[0x2000]=0x38; r[0x2001]=0;
  return r;
}
void prepare(GameGearMachine& m, unsigned backend, bool irq=false, unsigned im=1) {
  haltedAdvanceEnabled=false;
  m.loadRom(rom(irq));
  m.setIrComponents(nullptr,nullptr);
  if (backend==0) { Plugin::DefaultStepPolicy p; m.attachExecutorPolicy(p); }
  if (backend==1) { Plugin::VisibleStatePreservingStepPolicy p; m.attachExecutorPolicy(p); }
  if (backend==2) { Plugin::PortableIrStepPolicy p; m.attachExecutorPolicy(p); }
  auto r=m.debugRegisters(); r[5]=0x100; r[4]=0xcf00; r[12]=0x20; r[17]=im;
  m.debugCommitRegisters(r);
  for (unsigned n=0; !m.debugRegisters()[18] && n<32; ++n) m.step();
  require(m.debugRegisters()[18], "setup did not HALT");
}
void feedback(const CpuFeedback& a, const CpuFeedback& b) {
  require(a.pcBefore==b.pcBefore && a.pcAfter==b.pcAfter && a.retiredCycles==b.retiredCycles &&
      a.segmentBoundaryHint==b.segmentBoundaryHint && a.isControlFlow==b.isControlFlow &&
      a.executionPath==b.executionPath, "feedback mismatch");
}
void equivalent(GameGearMachine& a, GameGearMachine& b) {
  require(a.debugRegisters()==b.debugRegisters(), "CPU/refresh/deferred EI mismatch");
  require(a.deterministicStateFingerprint()==b.deterministicStateFingerprint(), "device/meta state mismatch");
  require(a.recentAudioSamples()==b.recentAudioSamples() && a.audioFrameCounter()==b.audioFrameCounter(),
      "audio stream mismatch");
  feedback(a.runtimeContext().getLastFeedback(), b.runtimeContext().getLastFeedback());
}
ExecutionSliceResult compare(GameGearMachine& a, GameGearMachine& b, ExecutionBudget budget,
    InstructionRetirementSink* oa=nullptr, InstructionRetirementSink* ob=nullptr) {
  haltedAdvanceEnabled=false; const auto x=a.runSlice(budget,oa);
  haltedAdvanceEnabled=true; const auto y=b.runSlice(budget,ob);
  require(x.progress.retiredInstructions==y.progress.retiredInstructions &&
      x.progress.retiredCycles==y.progress.retiredCycles && x.exitReason==y.exitReason, "slice exit/accounting mismatch");
  feedback(x.lastFeedback,y.lastFeedback); equivalent(a,b); return y;
}
struct Observer : InstructionRetirementSink {
  unsigned calls=0;
  InstructionRetirementDecision retireInstruction(const CpuFeedback&,const ExecutionSliceProgress&) override {
    ++calls; return InstructionRetirementDecision::continueSlice();
  }
};
struct Trace : IPlugin {
  using Event=std::tuple<unsigned,uint64_t,uint16_t,uint8_t,uint32_t>;
  std::vector<Event> events;
  std::string_view id() const override {return "halt-test";}
  void onMachineEvent(const MachineEvent& e,const MachineView&) override {
    events.emplace_back(unsigned(e.type),e.tick,e.address,e.value,e.feedback ? e.feedback->retiredCycles : 0u);
  }
};
struct CustomPolicy : Plugin::IExecutorPolicyPlugin {
  Plugin::DefaultStepPolicy p;
  std::unique_ptr<Plugin::IExecutorPolicyPlugin> clone() const override {return std::make_unique<CustomPolicy>(*this);}
  const Plugin::PluginMetadata& metadata() const override {return p.metadata();}
  ExecutionGuarantee guarantee() const override {return p.guarantee();}
  ExecutionBackend backend() const override {return p.backend();}
  bool shouldRecord(const Plugin::FetchBlock& f,const CpuFeedback& c) const override {return p.shouldRecord(f,c);}
  bool shouldSegment(const Plugin::FetchBlock& f,const CpuFeedback& c) const override {return p.shouldSegment(f,c);}
};
struct CustomBackend : IR::IIrExecutionBackend {
  bool supports(uint32_t a,uint32_t v) const noexcept override {
    return a==GameGearIR::kArchitectureId && v==IR::kIrAbiVersion;
  }
  BlockBackendArtifactPtr compile(const IR::BlockPtr&,std::string*) override {return {};}
  bool execute(const BlockBackendArtifact&,size_t,IR::InterpreterHost&,IR::InterpreterResult*) override {return false;}
};
void deadlines() {
  GameGearPSG a,b; a.writeData(0x90); b.writeData(0x90);
  GameGearVDP x,y;
  for (unsigned n=0;n<1500;++n) {
    const auto deadline=std::min(b.cyclesUntilNextSample(),y.cyclesUntilNextScanline());
    require(deadline>0 && deadline<=75, "invalid device deadline");
    const auto polls=(deadline-1)/4;
    for (unsigned i=0;i<polls;++i) { a.step(4); x.step(4); }
    if (polls) {b.step(polls*4);y.step(polls*4);}
    require(a.exportState()==b.exportState() && x.exportState()==y.exportState(), "interior device mismatch");
    a.step(4); b.step(4); x.step(4); y.step(4);
    require(a.exportState()==b.exportState() && x.exportState()==y.exportState(), "event device mismatch");
    require(x.takeScanlineReady()==y.takeScanlineReady() && x.takeVBlankEntered()==y.takeVBlankEntered() &&
        x.takeIrqAsserted()==y.takeIrqAsserted(), "device event order mismatch");
  }
  require(a.copyRecentSamples()==b.copyRecentSamples() && a.frameCounter()>0, "missing audio chunks");
}
void windows() {
  std::mt19937 rng(20261011);
  for (unsigned backend=0;backend<3;++backend) {
    GameGearMachine a,b; prepare(a,backend);prepare(b,backend);
    haltedAdvanceStats={};
    for (unsigned n=0;n<500;++n) {
      const auto budget=ExecutionBudget{rng()%513,rng()%4097,bool(rng()%2)};
      compare(a,b,budget);
      if(n%29==0) { a.setAnalysisInput(uint8_t(n));b.setAnalysisInput(uint8_t(n));a.serviceInput();b.serviceInput(); }
    }
    require(haltedAdvanceStats.bulkCalls>0 && haltedAdvanceStats.logicalIdlePolls>haltedAdvanceStats.bulkCalls*2,
        "no reduced polling work");
    require(b.audioFrameCounter()>0, "missing machine audio chunks");
    // Restore and ROM reload derive new deadlines; nothing is cached.
    const auto path=std::filesystem::temp_directory_path()/("time-halt-"+std::to_string(getpid())+".ptstate");
    a.save_state(path); a.load_state(path);b.load_state(path);std::filesystem::remove(path);
    compare(a,b,{1000,4001,false});
    prepare(a,backend);prepare(b,backend);compare(a,b,{1000,4001,false});
    auto ra=a.debugRegisters(),rb=b.debugRegisters();ra[19]=rb[19]=1;ra[16]=rb[16]=0;
    a.debugCommitRegisters(ra);b.debugCommitRegisters(rb);
    compare(a,b,{1000,4001,false});require(b.debugRegisters()[19]==1,"HALT changed current EI-delay semantics");
  }
}
void interrupts() {
  for(unsigned im=0;im<3;++im) {
    GameGearMachine a,b;prepare(a,0,true,im);prepare(b,0,true,im);
    bool woke=false; haltedAdvanceStats={};
    for(unsigned n=0;n<700;++n) { compare(a,b,{257,1025,false});woke |= !b.debugRegisters()[18]; }
    require(woke && haltedAdvanceStats.bulkCalls>0, "IRQ wake was not exercised");
  }
}
void emittedTransition() {
  const auto code=emittedGameGearCompute();
  auto bytes=rom(false);
  for(const auto& guard:code.block().guards)
    if(guard.kind==IR::GuardKind::CodeBytes)
      std::copy(guard.bytes.begin(),guard.bytes.end(),bytes.begin()+guard.subject);
  GameGearMachine a,b;a.loadRom(bytes);b.loadRom(bytes);
  for(auto* m:{&a,&b}) { auto r=m->debugRegisters();r[3]=0xc000;r[5]=code.block().guestStart;m->debugCommitRegisters(r); }
  auto binding=b.bindResearchBlock(code);
  const ExecutionBudget budget{code.block().instructions.size(),UINT64_MAX,false};
  haltedAdvanceEnabled=false;const auto x=a.runSlice(budget);
  haltedAdvanceEnabled=true;const auto y=b.runResearchBlock(binding,budget);
  require(x.progress.retiredInstructions==y.progress.retiredInstructions &&
      x.progress.retiredCycles==y.progress.retiredCycles &&
      a.deterministicStateFingerprint()==b.deterministicStateFingerprint(),"emitted active transition mismatch");
  for(auto* m:{&a,&b}) {auto r=m->debugRegisters();r[5]=0x100;m->debugCommitRegisters(r);
    for(unsigned n=0;!m->debugRegisters()[18] && n<32;++n)m->step();
    require(m->debugRegisters()[18],"emitted fallback setup did not halt");}
  haltedAdvanceStats={};compare(a,b,{10000,40001,false});
  require(haltedAdvanceStats.bulkCalls>0,"emitted fallback did not share halted advance");
}
void fallback() {
  GameGearMachine a,b;prepare(a,0);prepare(b,0);
  Observer oa,ob;haltedAdvanceStats={};compare(a,b,{100,400,false},&oa,&ob);
  require(oa.calls==100 && ob.calls==100 && !haltedAdvanceStats.bulkCalls,"observer did not force fallback");
  CustomPolicy policy;a.attachExecutorPolicy(policy);b.attachExecutorPolicy(policy);
  compare(a,b,{100,400,false});require(!haltedAdvanceStats.bulkCalls,"custom policy did not force fallback");
  prepare(a,1);prepare(b,1);
  a.setIrComponents(nullptr,std::make_unique<CustomBackend>());
  b.setIrComponents(nullptr,std::make_unique<CustomBackend>());
  haltedAdvanceStats={};compare(a,b,{100,400,false});
  require(!haltedAdvanceStats.bulkCalls,"custom IR backend did not force fallback");
  prepare(a,0);prepare(b,0);
  auto& ta=a.pluginManager().add(std::make_unique<Trace>());
  auto& tb=b.pluginManager().add(std::make_unique<Trace>());
  haltedAdvanceStats={};
  for(unsigned n=0;n<250;++n)compare(a,b,{100,400,false});
  require(ta.events==tb.events && !ta.events.empty() && !haltedAdvanceStats.bulkCalls,"plugin events/fallback mismatch");
  GameGearMachine c,d;prepare(c,0);prepare(d,0);Space::Capture ca,cb;
  c.setAnalysisCapture(&ca);d.setAnalysisCapture(&cb);haltedAdvanceStats={};
  compare(c,d,{32,128,false});require(!haltedAdvanceStats.bulkCalls,"capture did not force fallback");
  Space::Record ra,rb;
  while(ca.pop(ra)) { require(cb.pop(rb),"missing capture record");
    require(ra.kind==rb.kind && ra.address==rb.address && ra.value==rb.value && ra.cycles==rb.cycles,
      "capture record mismatch"); }
  require(!cb.pop(rb),"extra capture record");c.setAnalysisCapture(nullptr);d.setAnalysisCapture(nullptr);
}
}
int main() try {
  deadlines(); windows(); interrupts(); emittedTransition(); fallback();
  std::cout<<"Game Gear deadline, idle-credit, IRQ, audio/video, restore and fallback parity passed\n";
  return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
