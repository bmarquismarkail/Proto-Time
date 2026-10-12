#include "inst_cycle/research/HaltedAdvance.hpp"
#define TIME_RESEARCH_HALTED_ROM_STUDY 1
#define main wholeBlockOriginalMain
#include "measure_whole_block_roms.cpp"
#undef main
namespace {
template<class Core> void screen(unsigned index,bool smoke,Json& out){
 const auto& record=romCorpusManifest().at("cases").at(index);const auto code=emittedRomCorpus(index);
 ReplayExperimentOptions options;options.leanAccounting=true;options.haltBatch=128;options.reuseBindings=true;
 haltedAdvanceEnabled=false;const auto reference=replay<Core>(record,code,0,true,nullptr,options);
 for(unsigned variant=0;variant<8;++variant){haltedAdvanceEnabled=variant>=4;
   auto row=replay<Core>(record,code,variant%4,true,&reference,options);
   out["correctness"].push_back({{"core",record.at("core")},{"workload",record.at("workload")},
     {"backend",modes[variant%4]},{"advance",variant>=4},{"status","passed"},
     {"timelineSha256",row.at("timelineSha256")},{"windowsSha256",hashJson(row.at("windows"))},
     {"retirementsPerWindow",windowSize},{"romSha256",record.at("romSha256")}});
 }
 for(unsigned repeat=0;repeat<(smoke?1u:9u);++repeat)for(unsigned order=0;order<8;++order){
   const unsigned variant=(order+repeat)%8;haltedAdvanceEnabled=variant>=4;
   auto row=replay<Core>(record,code,variant%4,false,&reference,options);
   row.erase("windows");row.erase("timeline");
   row.update({{"core",record.at("core")},{"workload",record.at("workload")},
      {"backend",modes[variant%4]},{"advance",variant>=4},{"repeat",repeat},{"order",order},
      {"romSha256",record.at("romSha256")}});out["samples"].push_back(std::move(row));
 }
}
}
int main(int argc,char** argv)try{
 const bool smoke=argc==2 && std::string(argv[1])=="--smoke";
 if(argc>1 && !smoke)throw std::invalid_argument("usage: time-measure-halted-runtime [--smoke]");
 Json out={{"schema","proto-time-halted-runtime-v1"},{"qualification",false},{"admissionChanged",false},
   {"repetitions",smoke?1:9},{"haltSlicePollLimit",128},{"manifestSha256",hashJson(romCorpusManifest())},
   {"correctness",Json::array()},{"samples",Json::array()}};
 for(unsigned i=0;i<romCorpusManifest().at("cases").size();++i){
  if(romCorpusManifest().at("cases")[i].at("core")=="gameboy")screen<GB::GameBoyMachine>(i,smoke,out);
  else screen<GameGearMachine>(i,smoke,out);
 }
 std::cout<<out.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
