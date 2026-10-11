#include "inst_cycle/research/WholeBlockEmitter.hpp"
#include "tests/WholeBlockCorpus.hpp"
#include <fstream>
#include <iostream>
using namespace BMMQ::IR::Research;
int main(int argc,char** argv) try {
 if(argc!=2)return 2;
 std::ofstream out(argv[1]);out<<"#include <vector>\n";
 for(bool gb:{true,false}){
  std::vector<std::vector<unsigned char>> bytes={{0x01,0x34,0x12},{0x11,0x78,0x56},{0x21,0x00,0xc0},{0x31,0x00,0xc1},
    {0x03},{0x13},{0x23},{0x33},{0x0b},{0x1b},{0x2b},{0x3b},{0x20,0x02},{0x28,0x02},{0x30,0x02},{0x38,0x02},
    {0xc3,0x00,0x20},{0xcd,0x00,0x20},{0xc5},{0xd5},{0xe5},{0xf5},{0xc1},{0xd1},{0xe1},{0xf1},
    {static_cast<unsigned char>(gb?0xea:0x32),0x00,0xc0},{static_cast<unsigned char>(gb?0xfa:0x3a),0x00,0xc0}};
  if(gb)for(auto extra:std::vector<std::vector<unsigned char>>{{0xe0,0x43},{0xf0,0x43},{0xe2},{0xf2}})bytes.push_back(extra);
  std::string factory="namespace BMMQ::IR::Research { std::vector<CompiledBlock> "+std::string(gb?"extendedGameBoyFixtures":"extendedGameGearFixtures")+"(){return {";
  for(unsigned n=0;n<bytes.size();++n){
   CorpusSegment s;BMMQ::IR::SourceInstruction i{.address=0x1000,.length=static_cast<unsigned char>(bytes[n].size())};std::copy(bytes[n].begin(),bytes[n].end(),i.bytes.begin());s.instructions.push_back(i);
   auto name=std::string(gb?"emittedExtendedGB":"emittedExtendedGG")+std::to_string(n);
   out<<emitWholeBlock(*lowerCorpusSegment(gb,s,true),name,gb?GB::IRExecution::GameBoyCoreAdapter::kArchitectureId:BMMQ::GameGearIR::kArchitectureId);
   factory+=name+"(),";
  }
  out<<factory<<"};}}\n";
 }
 return out?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
