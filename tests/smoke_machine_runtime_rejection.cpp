#include "emulator/DynamicMachineProvider.hpp"
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <source_location>
void check(bool b,std::source_location at=std::source_location::current()) {if(!b) throw std::runtime_error("runtime rejection check line "+std::to_string(at.line()));}
template<class F> void rejects(F&& f) {bool b=false;try {f();}catch(const std::exception&){b=true;}check(b);}
int main(int argc,char** argv) {
    check(argc==2);
    auto* observation=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);check(observation);
    auto releases=reinterpret_cast<unsigned(*)()>(dlsym(observation,"time_test_runtime_releases"));
    auto discards=reinterpret_cast<unsigned(*)()>(dlsym(observation,"time_test_runtime_discards"));check(releases && discards);
    auto legacyCalls=reinterpret_cast<unsigned(*)()>(dlsym(observation,"time_test_runtime_legacy_calls"));check(legacyCalls);
    for(const auto* mode:{"module-version","family","region-count","clock"}) {
        setenv("TIME_TEST_RUNTIME_MODE",mode,1);BMMQ::MachineRegistry r;
        rejects([&]{BMMQ::registerDynamicMachineProviders(r,argv[1]);});check(r.descriptors().empty());
    }
    const auto prior=releases();
    for(const auto* mode:{"callback","table-version","table-size","decline"}) {
        setenv("TIME_TEST_RUNTIME_MODE",mode,1);BMMQ::MachineRegistry r;BMMQ::registerDynamicMachineProviders(r,argv[1]);
        rejects([&]{(void)r.create("runtime-test");});
    }
    check(releases()==prior+4);
    for(const auto* mode:{"prepare-fail","step-fail","cycles","overflow","state-budget","audio-bounds","video-bounds"}) {
        setenv("TIME_TEST_RUNTIME_MODE",mode,1);auto instance=BMMQ::createProvidedMachine("runtime-test",std::filesystem::path(argv[1]));auto& m=*instance.machine;
        const auto before=m.deterministicStateFingerprint();const auto discarded=discards();
        if(std::string_view(mode)=="prepare-fail") {
            rejects([&]{m.loadRom(std::vector<std::uint8_t>(32768));});check(m.deterministicStateFingerprint()==before);check(discards()==discarded+1);continue;
        }
        m.loadRom(std::vector<std::uint8_t>(32768));
        if(std::string_view(mode)=="state-budget") rejects([&]{m.save_state("/tmp/runtime-state-budget-must-not-exist.ptime");});
        else if(std::string_view(mode)=="audio-bounds") rejects([&]{m.recentAudioSamples();});
        else if(std::string_view(mode)=="video-bounds") rejects([&]{m.realtimeVideoPacket({160,144});});
        else {rejects([&]{m.step();});check(!m.stopSummary().empty());rejects([&]{m.step();});}
    }
    check(legacyCalls()==0);
    unsetenv("TIME_TEST_RUNTIME_MODE");dlclose(observation);
    std::cout<<"C runtime ABI rollback, cleanup, admission, output budgets and faulted retirement passed\n";
}
