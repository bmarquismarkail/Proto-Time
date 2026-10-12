#include "WholeBlockCorpus.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include <iostream>
namespace BMMQ::IR::Research {
std::vector<CompiledBlock> extendedGameBoyFixtures();
std::vector<CompiledBlock> extendedGameGearFixtures();
}
using namespace BMMQ;using namespace BMMQ::IR::Research;
void require(bool c,const char* message){if(!c)throw std::runtime_error(message);}
template<class Core>void check(const std::vector<CompiledBlock>& code,bool gb){
 for(const auto& block:code)for(unsigned seed:{0u,1u,127u,128u,255u})for(bool direct:{false,true}){
  std::vector<std::uint8_t> rom(32768);
  for(const auto& g:block.block().guards)if(g.kind==IR::GuardKind::CodeBytes)std::copy(g.bytes.begin(),g.bytes.end(),rom.begin()+g.subject);
  Core baseline,candidate;baseline.loadRom(rom);candidate.loadRom(rom);
  for(auto* m:{&baseline,&candidate}){
   auto r=m->debugRegisters();r[0]=(seed<<8)|(gb?(seed&0xf0):seed);r[1]=0xff43;r[2]=0xffff;r[3]=0xc000;r[4]=0xc100;r[5]=0x1000;
   require(m->debugValidateRegisters(r),"invalid extended registers");m->debugCommitRegisters(r);
   m->debugCommitByte(0xc000,seed);m->debugCommitByte(0xc100,0xff);m->debugCommitByte(0xc101,0x81);
  }
  auto bound=candidate.bindResearchBlock(block);auto expected=baseline.runSlice({.maxInstructions=1});
  auto actual=candidate.runResearchBlock(bound,{.maxInstructions=1},nullptr,direct);
  require(actual.progress.retiredInstructions==1&&actual.progress.retiredCycles==expected.progress.retiredCycles,"extended cycles mismatch");
  if(baseline.debugRegisters()!=candidate.debugRegisters() || baseline.deterministicStateFingerprint()!=candidate.deterministicStateFingerprint()){
   std::cerr<<(gb?"gb":"gg")<<" opcode "<<block.block().guards.back().bytes[0]<<" seed "<<seed<<" direct "<<direct<<'\n';
   throw std::runtime_error("extended full-state mismatch");
  }
  const auto& a=actual.lastFeedback;const auto& e=expected.lastFeedback;
  require(a.pcBefore==e.pcBefore&&a.pcAfter==e.pcAfter&&a.retiredCycles==e.retiredCycles&&a.isControlFlow==e.isControlFlow&&a.segmentBoundaryHint==e.segmentBoundaryHint,"extended feedback mismatch");
 }
}
int main()try{check<GB::GameBoyMachine>(extendedGameBoyFixtures(),true);check<GameGearMachine>(extendedGameGearFixtures(),false);std::cout<<"extended differential checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
