// Research measurements: timing never grants backend admission.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {
using Json = nlohmann::json;
std::vector<std::uint8_t> rom(bool gb, unsigned workload) {
    std::vector<std::uint8_t> bytes(32768, 0);
    std::vector<std::uint8_t> code;
    if (workload == 0) code = {0x04,0x0c,0x80,0x81,0x2f};
    else if (workload == 1) code = {0x21,0x00,0xc0,0x34,0x7e,0x3c,0x77};
    else code = gb ? std::vector<std::uint8_t>{0x3e,0x80,0xe0,0x12,0xf0,0x00}
                   : std::vector<std::uint8_t>{0x3e,0x90,0xd3,0x7f,0xdb,0xdc};
    const auto displacement = static_cast<std::uint8_t>(-static_cast<int>(code.size()+2));
    code.push_back(0x18); code.push_back(displacement);
    std::copy(code.begin(),code.end(),bytes.begin()+(gb?256:0));
    return bytes;
}
std::string hashRom(const std::vector<std::uint8_t>& bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned size=0;
    if(EVP_Digest(bytes.data(),bytes.size(),digest,&size,EVP_sha256(),nullptr)!=1)
        throw std::runtime_error("ROM hashing failed");
    std::ostringstream out;
    for(unsigned i=0;i<size;++i) out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(digest[i]);
    return out.str();
}
template<class Core> Json run(bool gb, unsigned workload, unsigned mode, std::uint64_t steps) {
    Core machine;
    machine.loadRom(rom(gb,workload));
    if (mode == 1) { BMMQ::Plugin::VisibleStatePreservingStepPolicy p; machine.attachExecutorPolicy(p); }
    if (mode == 2) { BMMQ::Plugin::PortableIrStepPolicy p; machine.attachExecutorPolicy(p); }
    auto advance = [&](std::uint64_t count) {
        std::uint64_t cycles=0;
        while(count) {
            auto s=machine.runSlice({.maxInstructions=std::min<std::uint64_t>(256,count),.stopOnSegmentBoundary=false});
            if(!s.progress.retiredInstructions || s.progress.retiredInstructions>count)
                throw std::runtime_error("invalid retirement progress");
            count-=s.progress.retiredInstructions; cycles+=s.progress.retiredCycles;
        }
        return cycles;
    };
    advance(4096);
    auto start=std::chrono::steady_clock::now();
    const auto cycles=advance(steps);
    const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    return {{"nanoseconds",nanos},{"cycles",cycles},{"fingerprint",machine.deterministicStateFingerprint()}};
}
}
int main(int argc, char** argv) try {
    const bool smoke=argc==2 && std::string(argv[1])=="--smoke";
    if(argc>1 && !smoke) throw std::invalid_argument("usage: time-measure-acceleration-corpus [--smoke]");
    Json output={{"schema",1},{"warmupInstructions",4096},{"measuredInstructions",smoke?512:100000},
                 {"repetitions",smoke?1:9},{"samples",Json::array()},
                 {"scope","synthetic baseline comparison; no native backend admission"}};
    for(bool gb:{true,false}) for(unsigned workload=0;workload<3;++workload) {
        const char* names[]={"compute","ram","devices"};
        const char* modes[]={"baseline","cached-block","portable-ir"};
        std::string reference; std::uint64_t referenceCycles=0;
        for(unsigned repeat=0;repeat<(smoke?1u:9u);++repeat) for(unsigned order=0;order<3;++order) {
            const unsigned mode=(order+repeat)%3; // Rotate order to reduce drift bias.
            Json result=gb?run<GB::GameBoyMachine>(true,workload,mode,smoke?512:100000)
                          :run<BMMQ::GameGearMachine>(false,workload,mode,smoke?512:100000);
            if(reference.empty()){reference=result.at("fingerprint");referenceCycles=result.at("cycles");}
            if(result.at("fingerprint")!=reference || result.at("cycles")!=referenceCycles)
                throw std::runtime_error("backend state/cycle mismatch");
            result["core"]=gb?"gameboy":"gamegear"; result["workload"]=names[workload];
            result["backend"]=modes[mode];result["repeat"]=repeat;
            result["romSha256"]=hashRom(rom(gb,workload));
            output["samples"].push_back(std::move(result));
        }
    }
    std::cout<<output.dump(2)<<'\n';
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
