// Active-only diagnostic lane; never feeds throughput admission.
#define main original_synthetic_corpus_main
#include "measure_whole_block_corpus.cpp"
#undef main

Json phases(const PhaseLedger& ledger) {
  Json rows=Json::array();
  for(unsigned i=0;i<ledger.phases.size();++i) {
    const auto& d=ledger.phases[i];
    rows.push_back({{"phase",phaseNames[i]},{"calls",d.calls},{"samples",d.samples},{"nanoseconds",d.nanoseconds}});
  }
  return {{"phases",rows},{"metadataCopies",ledger.metadataCopies},{"estimatedMetadataBytes",ledger.estimatedMetadataBytes}};
}
int main(int argc,char** argv) try {
  bool smoke=false;unsigned repetitions=3;
  for(int i=1;i<argc;++i) {
    std::string arg=argv[i];
    if(arg=="--smoke") {smoke=true;repetitions=1;}
    else if(arg=="--repetitions"&&i+1<argc) repetitions=std::stoul(argv[++i]);
    else throw std::invalid_argument("invalid active diagnostic option");
  }
  require(repetitions>0&&repetitions<=27,"invalid repetitions");
  Json output={{"schema","proto-time-active-phase-diagnostics-v1"},{"diagnosticBuild",true},{"profileTimings",true},{"admissionChanged",false},{"repetitions",repetitions},{"samples",Json::array()},{"correctness",Json::array()},
    {"method",{{"sampling","independent xorshift per phase, 1/64"},{"nestedInclusive",true},{"registerScope","IR host access and emitted direct view; baseline and cached CPU internals uninstrumented"},{"bindingOutsideSteadyState",true},{"warmupInstructions",4096},{"instructions",smoke?512:100000},{"haltInstructions",0}}}};
  PhaseLedger calibration;
  {PhaseSession session(calibration);for(unsigned i=0;i<1000000;++i){TIME_RESEARCH_SCOPE(empty,Phase::Dispatch);}}
  output["emptyScopeCalibration"]=phases(calibration);
  auto run=[&]<class Core>(bool gb,unsigned w) {
    const auto program=corpusProgram(gb,w);
    const auto code=gb?emittedGameBoyCorpus(w):emittedGameGearCorpus(w);
    std::uint16_t branch=0;
    for(const auto& segment:program.segments)for(const auto& instruction:segment.instructions) {
      require(!instruction.bytes.empty()&&instruction.bytes[0]!=0x76,"active fixture includes HALT");
      if(instruction.bytes[0]==0x20)branch=instruction.address;
    }
    for(unsigned mode=1;mode<5;++mode) {
      std::uint64_t taken=0,notTaken=0;
      for(unsigned seed:{0u,127u,128u,255u}) {
      Core baseline,candidate;prepare(baseline,program,seed,0);prepare(candidate,program,seed,std::min(mode,3u));
      Runner<Core> runner(candidate,program,code,mode>=3,{.directRegisters=mode==4});
      Compare<Core> observer(baseline,candidate,branch);runner.advance(smoke?256:1024,&observer);
      require(observer.count==(smoke?256u:1024u),"retirement comparison incomplete");
      taken+=observer.taken;notTaken+=observer.notTaken;
      }
      require(taken&&notTaken,"missing conditional paths");
    }
    output["correctness"].push_back({{"core",gb?"gameboy":"gamegear"},{"workload",corpusNames[w]},{"status","passed"}});
    Json expected;
    for(unsigned repeat=0;repeat<repetitions;++repeat)for(unsigned order=0;order<5;++order) {
      unsigned mode=(order+repeat)%5;
      Core machine;prepare(machine,program,128,std::min(mode,3u));
      PhaseLedger binding,steady;
      for(auto& phase:steady.phases)phase.rng^=(repeat+1)*0x85ebca6bu;
      std::unique_ptr<Runner<Core>> runner;
      auto bindingStart=std::chrono::steady_clock::now();
      {PhaseSession session(binding);runner=std::make_unique<Runner<Core>>(machine,program,code,mode>=3,ReplayExperimentOptions{.directRegisters=mode==4});}
      const auto bindingElapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-bindingStart).count();
      runner->advance(4096);
      Accounting total;std::int64_t elapsed;
      {PhaseSession session(steady);auto start=std::chrono::steady_clock::now();total=runner->advance(smoke?512:100000);elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
      Json state={{"registers",machine.debugRegisters()},{"fingerprint",machine.deterministicStateFingerprint()},{"instructions",total.instructions},{"cycles",total.cycles}};
      if(expected.is_null())expected=state;else require(expected==state,"active diagnostic endpoint mismatch");
      require(steady.phases[static_cast<unsigned>(Phase::Bind)].calls==0,"binding leaked into steady state");
      require(steady.metadataCopies==0,"unexpected steady metadata copies");
      const auto calls=[&](Phase phase){return steady.phases[static_cast<unsigned>(phase)].calls;};
      require(calls(Phase::CpuStep)+calls(Phase::CpuRetirement)==total.instructions,"CPU instrumentation incomplete");
      require(calls(Phase::EntryGuard)+calls(Phase::ContinuationGuard)==total.emittedInstructions,"guard instrumentation incomplete");
      require(steady.phases[static_cast<unsigned>(Phase::MachineRetirement)].calls==total.instructions,"device retirement instrumentation incomplete");
      auto row=total.json();row.update({{"core",gb?"gameboy":"gamegear"},{"workload",corpusNames[w]},{"backend",mode==4?"direct":modes[mode]},{"repeat",repeat},{"order",order},{"romSha256",hashRom(program.rom)},{"finalState",state},{"instrumentedNanoseconds",elapsed},{"instrumentedRunnerSetupNanoseconds",bindingElapsed},{"bindingDiagnostics",phases(binding)},{"steadyDiagnostics",phases(steady)}});
      output["samples"].push_back(row);
    }
  };
  for(unsigned w=0;w<3;++w){run.template operator()<GB::GameBoyMachine>(true,w);run.template operator()<GameGearMachine>(false,w);}
  std::cout<<output.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
