#include "LockstepService.hpp"
#include "time_netplay_source.hpp"
#include "space/Project.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <nlohmann/json.hpp>
#ifndef TIME_NETPLAY_SOURCE_SHA256
#error Netplay must be built with an exact source identity
#endif
using namespace BMMQ;
using namespace BMMQ::Netplay;
namespace {
std::uint64_t number(const std::string& text,std::uint64_t maximum) {
    std::size_t used{};const auto value=std::stoull(text,&used,0);
    if(text.empty() || text.front()=='-' || used!=text.size() || value>maximum) throw std::invalid_argument("invalid netplay numeric option");
    return value;
}
std::string hex(const Digest& digest) {
    constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto value:digest) {result.push_back(digits[value>>4]);result.push_back(digits[value&15]);}return result;
}
}
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;++i) {
            const std::string key=argv[i];
            if(key!="--core" && key!="--rom" && key!="--bind-port" && key!="--peer-port" && key!="--bind-address" && key!="--peer-address" &&
               key!="--session" && key!="--peer" && key!="--frames" && key!="--input" && key!="--configuration" && key!="--timeout-ms")
                throw std::invalid_argument("unknown netplay option: "+key);
            if(i+1>=argc || !options.emplace(key,argv[++i]).second) throw std::invalid_argument("missing or duplicate netplay option");
        }
        for(auto key:{"--core","--rom","--bind-port","--peer-port","--session","--peer"}) if(!options.contains(key)) throw std::invalid_argument(std::string("required: ")+key);
        const auto core=options.at("--core");
        std::unique_ptr<Machine> machine;
        if(core=="gameboy") machine=std::make_unique<GB::GameBoyMachine>();
        else if(core=="gamegear") machine=std::make_unique<GameGearMachine>();
        else throw std::invalid_argument("netplay core must be gameboy or gamegear");
        std::ifstream input(options.at("--rom"),std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<0 || input.tellg()>16*1024*1024) throw std::invalid_argument("netplay ROM missing or exceeds 16 MiB");
        std::vector<std::uint8_t> rom(std::size_t(input.tellg()));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(rom.data()),std::streamsize(rom.size()))) throw std::invalid_argument("unable to read ROM");
        machine->loadRom(rom);
        const auto peer=std::uint8_t(number(options.at("--peer"),1));
        const auto frames=options.contains("--frames")?number(options.at("--frames"),4096):60;
        if(!frames) throw std::invalid_argument("frames must be positive");
        const auto mask=InputButtonMask(options.contains("--input")?number(options.at("--input"),255):0);
        const auto timeout=options.contains("--timeout-ms")?number(options.at("--timeout-ms"),60000):10000;
        RemoteTransportConfig transportConfig;
        transportConfig.localPort=std::uint16_t(number(options.at("--bind-port"),65535));
        transportConfig.peerPort=std::uint16_t(number(options.at("--peer-port"),65535));
        if(options.contains("--bind-address"))transportConfig.localAddress=options.at("--bind-address");
        if(options.contains("--peer-address"))transportConfig.peerAddress=options.at("--peer-address");
        transportConfig.disconnectTimeout=std::chrono::milliseconds(timeout);
        const auto romDigest=Space::digest(rom);
        const nlohmann::json config{{"schemaVersion",1},{"sourceSha256",TIME_NETPLAY_SOURCE_SHA256},{"core",core},
            {"romSha256",romDigest},{"clockHz",machine->clockHz()},{"backend","baseline"},{"ownership",{255,0}},
            {"extraConfiguration",options.contains("--configuration")?options.at("--configuration"):""}};
        const auto text=config.dump();const auto configDigest=Space::digest(std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
        Binding binding{.core=core=="gameboy"?Core::GameBoy:Core::GameGear,.session=digestFromHex(options.at("--session")),
            .rom=digestFromHex(romDigest),.configuration=digestFromHex(configDigest),.generation=1};
        RemoteTransport transport;
        if(!transport.start(transportConfig)) throw std::runtime_error("unable to start remote input transport");
        LockstepService service(*machine,binding,peer,&transport);
        nlohmann::json report{{"schemaVersion",1},{"core",core},{"peer",peer},{"sourceSha256",TIME_NETPLAY_SOURCE_SHA256},
            {"romSha256",romDigest},{"configurationSha256",configDigest},{"frames",nlohmann::json::array()}};
        for(std::uint64_t frame=0;frame<frames;++frame) {
            if(!service.submitLocal(mask)) throw std::runtime_error("unable to submit local frame input");
            std::uint64_t instructions{},cycles{};Progress progress;
            do {
                progress=service.run(512);instructions+=progress.instructions;cycles+=progress.cycles;
                if(progress.state==State::Faulted) throw std::runtime_error("netplay paused with fault "+std::to_string(unsigned(progress.fault)));
                if(progress.state==State::Waiting) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while(progress.state!=State::FrameComplete);
            report["frames"].push_back({{"frame",frame},{"instructions",instructions},{"cycles",cycles},{"carry",progress.carry},
                {"before",hex(progress.before)},{"after",hex(progress.after)}});
        }
        // The next frame's packet acknowledges the final complete state. No
        // instruction of that frame executes and its input is never published.
        if(!service.submitLocal(0)) throw std::runtime_error("unable to acknowledge final state");
        while(!service.acknowledged()) {
            if(service.engine().fault()!=Fault::None) throw std::runtime_error("final state acknowledgment failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto stats=transport.diagnostics();
        report["transport"]={{"sent",stats.sent},{"received",stats.received},{"retransmitted",stats.retransmitted}};
        report["finalFingerprint"]=hex(service.before());report["status"]="passed";service.disconnect();
        std::cout<<report.dump()<<'\n';return 0;
    } catch(const std::exception& error) {
        std::cerr<<nlohmann::json{{"status","paused"},{"error",error.what()}}.dump()<<'\n';return 1;
    }
}
