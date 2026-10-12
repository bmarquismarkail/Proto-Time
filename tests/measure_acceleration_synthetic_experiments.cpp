#define main original_synthetic_corpus_main
#include "measure_whole_block_corpus.cpp"
#undef main
int main(int argc,char** argv) try {
  bool smoke=false;unsigned repetitions=9,seed=20261009;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--smoke") {smoke=true;repetitions=1;}
    else if(arg=="--seed"&&i+1<argc)seed=std::stoul(argv[++i]);
    else if(arg=="--repetitions"&&i+1<argc)repetitions=std::stoul(argv[++i]);
    else throw std::invalid_argument("invalid synthetic experiment option");
  }
  if(repetitions!=1&&repetitions!=9&&repetitions!=27)throw std::invalid_argument("invalid repetitions");
  Json output={{"schema","proto-time-acceleration-synthetic-experiments-v2"},{"samples",Json::array()},{"correctness",Json::array()},{"repetitions",repetitions},{"orderSeed",seed},{"profileTimings",false},{"diagnosticBuild",false}};
  auto run=[&]<class Core>(bool gb,unsigned w){
    const auto program=corpusProgram(gb,w);
    const auto code=gb?emittedGameBoyCorpus(w):emittedGameGearCorpus(w);
    std::uint16_t branch=0;for(const auto& s:program.segments)for(const auto& i:s.instructions)if(i.bytes[0]==0x20)branch=i.address;
    for(unsigned seed:{0u,127u,128u,255u}){
      Core baseline,candidate;prepare(baseline,program,seed,0);prepare(candidate,program,seed,3);
      Runner<Core> runner(candidate,program,code,true,{.directRegisters=true});
      Compare<Core> observer(baseline,candidate,branch);runner.advance(smoke?256:1024,&observer);
      require(observer.count==(smoke?256u:1024u),"direct retirement count mismatch");
    }
    output["correctness"].push_back({{"core",gb?"gameboy":"gamegear"},{"workload",corpusNames[w]},{"status","passed"}});
    Json expected;
    for(unsigned repeat=0;repeat<repetitions;++repeat)for(unsigned order=0;order<5;++order){
      const unsigned mode=(order+repeat+seed%5)%5;
      Core machine;prepare(machine,program,128,std::min(mode,3u));
      Runner<Core> runner(machine,program,code,mode>=3,{.directRegisters=mode==4});
      runner.advance(4096);auto start=std::chrono::steady_clock::now();auto total=runner.advance(smoke?512:100000);
      auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
      Json state={{"registers",machine.debugRegisters()},{"fingerprint",machine.deterministicStateFingerprint()},{"instructions",total.instructions},{"cycles",total.cycles}};
      if(expected.is_null())expected=state;else require(expected==state,"synthetic direct endpoint mismatch");
      auto row=total.json();row["finalState"]=state;row.update({{"core",gb?"gameboy":"gamegear"},{"workload",corpusNames[w]},{"backend",mode==4?"direct":modes[mode]},{"nanoseconds",elapsed},{"repeat",repeat},{"order",order}});
      output["samples"].push_back(row);
    }
  };
  for(unsigned w=0;w<3;++w){run.template operator()<GB::GameBoyMachine>(true,w);run.template operator()<GameGearMachine>(false,w);}
  std::cout<<output.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
