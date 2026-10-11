// Supplemental research: complete oracle, balanced variants and backend order.
#define main original_rom_corpus_main
#include "measure_whole_block_roms.cpp"
#undef main
#include <random>

int main(int argc, char** argv) try {
  unsigned repetitions=9, selectedCase=4, seed=20261009;
  std::string selectedVariant;
  bool windows=true, profile=false, ablations=false;
  for(int i=1;i<argc;++i) {
    std::string arg=argv[i];
    if(arg=="--smoke") repetitions=1;
    else if(arg=="--screen") repetitions=3;
    else if(arg=="--repetitions" && i+1<argc) repetitions=std::stoul(argv[++i]);
    else if(arg=="--seed" && i+1<argc) seed=std::stoul(argv[++i]);
    else if(arg=="--case" && i+1<argc) selectedCase=std::stoul(argv[++i]);
    else if(arg=="--variant" && i+1<argc) selectedVariant=argv[++i];
    else if(arg=="--no-windows") windows=false;
    else if(arg=="--profile") profile=true;
    else if(arg=="--ablations") ablations=true;
    else throw std::invalid_argument("invalid experiment option");
  }
  if(selectedCase>4 || (repetitions!=1&&repetitions!=3&&repetitions!=9&&repetitions!=27)) throw std::invalid_argument("invalid experiment limits");
  struct Variant {const char* name; ReplayExperimentOptions options; bool expanded=false; bool ablation=false;};
  std::vector<Variant> variants={
    {"original",{}}, {"lean",{.leanAccounting=true}},
    {"halt64",{.leanAccounting=true,.haltBatch=64}},
    {"halt512",{.leanAccounting=true,.haltBatch=512},false,true},
    {"reuse",{.reuseBindings=true}}, {"reuse-copy",{.reuseBindings=true,.copyBindings=true}},
    {"direct",{.directRegisters=true}}, {"expanded",{},true},
    {"reuse-direct",{.reuseBindings=true,.directRegisters=true}},
    {"lean-halt-reuse-direct",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true,.directRegisters=true}},
    {"all",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true,.directRegisters=true},true,true},
    {"halt512-copy",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true,.copyBindings=true},false,true},
    {"halt512-reuse",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true},false,true},
    {"halt512-direct",{.leanAccounting=true,.haltBatch=512,.directRegisters=true},false,true},
    {"halt512-expanded",{.leanAccounting=true,.haltBatch=512},true,true},
    {"halt512-expanded-copy",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true,.copyBindings=true},true,true},
    {"halt512-expanded-reuse",{.leanAccounting=true,.haltBatch=512,.reuseBindings=true},true,true}
  };
  std::erase_if(variants,[&](const auto& v){return (!selectedVariant.empty()&&selectedVariant!=v.name)||(ablations&&!v.ablation);});
  if(variants.empty()) throw std::invalid_argument("unknown/empty variant selection");
  if(profile) for(auto& v:variants) v.options.profileTimings=true;
  std::mt19937 rng(seed);
  std::shuffle(variants.begin(),variants.end(),rng);
  Json output={{"schema","proto-time-acceleration-experiments-v2"},
    {"scope","supplemental research; historical evidence retained; native ARM64 deferred"},
    {"repetitions",repetitions},{"retirementWindows",windows},{"profileTimings",profile},
    {"diagnosticBuild",false},{"orderSeed",seed},{"samples",Json::array()},{"correctness",Json::array()}};
#ifdef TIME_RESEARCH_PHASES
  output["diagnosticBuild"]=true;
#endif
  for(unsigned c=0;c<4;++c) {
    if(selectedCase<4&&c!=selectedCase) continue;
    const auto& original=romCorpusManifest().at("cases").at(c);
    const auto code=emittedRomCorpus(c);
    auto run=[&](const Json& record,const auto& artifact,unsigned mode,bool observed,const Json* ref,ReplayExperimentOptions opt) {
      return original.at("core")=="gameboy" ? replay<GB::GameBoyMachine>(record,artifact,mode,observed,ref,opt) : replay<GameGearMachine>(record,artifact,mode,observed,ref,opt);
    };
    const auto reference=run(original,code,0,windows,nullptr,{});
    auto execute=[&](const Variant& v,unsigned mode,bool observed) {
      const auto& record=v.expanded?expandedRomCorpusManifest().at("cases").at(c):original;
      const auto artifact=v.expanded?emittedExpandedRomCorpus(c):code;
      return run(record,artifact,mode,observed,&reference,v.options);
    };
    for(const auto& v:variants) {
      std::cerr<<"CHECK "<<v.name<<" case "<<c<<'\n';
      auto checked=execute(v,3,windows);
      checked["windowCount"]=checked.at("windows").size(); checked["retirementsPerWindow"]=windowSize;
      checked["status"]="passed"; checked["core"]=original.at("core"); checked["workload"]=original.at("workload");
      checked.erase("windows");checked.erase("timeline");checked["variant"]=v.name;checked["case"]=c;
      output["correctness"].push_back(checked);
    }
    for(unsigned repeat=0;repeat<repetitions;++repeat) for(unsigned vorder=0;vorder<variants.size();++vorder) {
      const auto& v=variants[(vorder+repeat)%variants.size()];
      std::cerr<<"VARIANT "<<v.name<<" case "<<c<<" repeat "<<repeat<<'\n';
      for(unsigned order=0;order<4;++order) {
        const unsigned mode=(order+repeat+seed%4)%4;
        auto result=execute(v,mode,false);result.erase("windows");result.erase("timeline");
        result.update({{"variant",v.name},{"case",c},{"core",original.at("core")},{"workload",original.at("workload")},
          {"backend",modes[mode]},{"repeat",repeat},{"order",order},{"variantOrder",vorder},
          {"lean",v.options.leanAccounting},{"haltBatch",v.options.haltBatch},{"reuse",v.options.reuseBindings},
          {"direct",v.options.directRegisters},{"expanded",v.expanded},{"copyBindings",v.options.copyBindings}});
        output["samples"].push_back(std::move(result));
      }
    }
  }
  std::cout<<output.dump(2)<<'\n';return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
