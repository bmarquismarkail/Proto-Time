#include "HttpServer.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/Project.hpp"
#include "time_netplay_source.hpp"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sys/random.h>
#include <unistd.h>
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int){interrupted=1;}
std::string read(const std::filesystem::path& path,std::size_t max) {
    const auto size=std::filesystem::file_size(path);if(!size || size>max)throw std::invalid_argument("resource size outside budget");
    std::string text(size,'\0');std::ifstream file(path,std::ios::binary);if(!file.read(text.data(),text.size()))throw std::runtime_error("resource read failed");return text;
}
unsigned port(const std::string& text){std::uint64_t value{};std::size_t used{};value=std::stoull(text,&used);if(used!=text.size() || text.empty() || text[0]=='-' || value>65535)throw std::invalid_argument("invalid port");return value;}
}
int main(int argc,char** argv) {
    using namespace BMMQ;using namespace RemoteVideo;using Json=nlohmann::json;
    try {
        std::string core="gameboy";std::filesystem::path romPath,assets;unsigned selectedPort=0;
        char exe[4096];const auto n=readlink("/proc/self/exe",exe,sizeof(exe));
        if(n<=0 || std::size_t(n)==sizeof(exe))throw std::runtime_error("executable path unavailable");
        assets=std::filesystem::path(std::string(exe,n)).parent_path()/"browser-assets";
        for(int i=1;i<argc;++i) {const std::string key=argv[i];if(i+1>=argc)throw std::invalid_argument("option requires value");
            if(key=="--core")core=argv[++i];else if(key=="--rom")romPath=argv[++i];else if(key=="--port")selectedPort=port(argv[++i]);else if(key=="--assets")assets=argv[++i];else throw std::invalid_argument("unknown option");}
        if(romPath.empty())throw std::invalid_argument("usage: time-browser --rom ROM [--core gameboy|gamegear] [--port PORT] [--assets DIRECTORY]");
        Inspector inspector(core);const auto rawRom=read(romPath,16*1024*1024);std::vector<std::uint8_t> rom(rawRom.begin(),rawRom.end());
        const auto hash=Space::digest(rom);
        std::array<std::uint8_t,32> random;std::size_t done=0;
        while(done<random.size()){auto count=getrandom(random.data()+done,random.size()-done,0);if(count<=0)throw std::runtime_error("session token unavailable");done+=count;}
        constexpr char hex[]="0123456789abcdef";std::string token;for(auto byte:random){token+=hex[byte>>4];token+=hex[byte&15];}
        HttpServer server(selectedPort,token,read(assets/"index.html",262144),read(assets/"inspector.css",262144),read(assets/"inspector.js",262144),read(assets/"postprocess.js",262144));
        struct Ready {Debug::DebugService* service;std::vector<std::string> registers;std::uint32_t clock;};
        std::promise<Ready> startup;auto ready=startup.get_future();Mailbox mailbox;std::atomic<bool> failed{false};
        std::jthread machineLane([&](std::stop_token stop) {
            bool started=false;
            try {
                std::unique_ptr<Machine> machine;
                if(core=="gameboy")machine=std::make_unique<GB::GameBoyMachine>();else machine=std::make_unique<GameGearMachine>();
                machine->loadRom(rom);Debug::DebugService debugger(*machine,false);
                std::vector<std::string> names;for(auto name:debugger.registerNames())names.emplace_back(name);
                startup.set_value({&debugger,names,machine->clockHz()});started=true;
                auto nextPublish=std::chrono::steady_clock::now();auto paceStart=nextPublish;
                std::uint64_t sequence=0,cycles=0,pacedCycles=0;
                Debug::Reply lastPublishedCPU{}; bool published=false;
                try { while(!stop.stop_requested()) {
                    debugger.pump();const auto prior=debugger.currentSnapshot();
                    const auto now=std::chrono::steady_clock::now();
                    if(prior.state==Debug::State::Running) {
                        const auto target=paceStart+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(pacedCycles)/machine->clockHz()));
                        if(target<=now) {const auto result=debugger.run(64);cycles+=result.progress.retiredCycles;pacedCycles+=result.progress.retiredCycles;}
                    } else {paceStart=now;pacedCycles=0;}
                    const auto current=debugger.currentSnapshot();
                    const bool changed=!published || current.state!=lastPublishedCPU.state ||
                        current.generation!=lastPublishedCPU.generation || current.pauseId!=lastPublishedCPU.pauseId ||
                        current.rejectedActions!=lastPublishedCPU.rejectedActions || current.requestOverflow!=lastPublishedCPU.requestOverflow;
                    // Paused guest state has one writer and cannot change
                    // without one of the lifecycle/control identities above.
                    // Rebuilding identical paused atlases can delay the next
                    // meaningful publication on slower inspection hosts.
                    if(changed || (current.state==Debug::State::Running && now>=nextPublish)) {
                        auto frame=machine->realtimeVideoPacket({160,144});auto video=machine->videoStateSnapshot();
                        if(!frame || !video || !mailbox.publish({current,std::move(frame->packet),std::move(*video),++sequence,cycles}))throw std::runtime_error("snapshot capability rejected");
                        lastPublishedCPU=current; published=true;
                        nextPublish=now+std::chrono::milliseconds(current.state==Debug::State::Running?16:1000);
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                } } catch(...) {
                    failed.store(true,std::memory_order_release);
                    // Keep the published service alive until the transport has stopped.
                    while(!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } catch(...) {failed.store(true,std::memory_order_release);if(!started)startup.set_exception(std::current_exception());}
        });
        const auto connection=ready.get();
        Json meta{{"schemaVersion",1},{"core",core},{"romSha256",hash},{"sourceSha256",TIME_NETPLAY_SOURCE_SHA256},
                  {"registerNames",connection.registers},{"clockHz",connection.clock},{"token",token},{"backend","baseline"},
                  {"stream","owned latest snapshots"},{"address","http://127.0.0.1:"+std::to_string(server.port())}};
        std::cout<<Json{{"url",meta["address"]},{"core",core},{"romSha256",hash},{"sourceSha256",TIME_NETPLAY_SOURCE_SHA256}}.dump()<<std::endl;
        std::signal(SIGINT,interrupt);std::signal(SIGTERM,interrupt);
        std::string state="{\"error\":\"first snapshot pending\"}";
        // At most one owned decode is in flight. Network deadlines and command
        // acknowledgements must not wait for atlas construction or JSON work.
        std::future<std::string> decoded;
        std::optional<Snapshot> pending;
        std::string videoBody, cpuBody;
        std::uint64_t latestSequence=0, latestCycles=0, decodeDrops=0;
        while(!interrupted && !failed.load(std::memory_order_acquire)) {
            bool changed=false;
            if(auto snapshot=mailbox.consume()) {
                cpuBody=Inspector::reply(snapshot->cpu).dump();
                latestSequence=snapshot->sequence;latestCycles=snapshot->cycles;
                if(pending)++decodeDrops;
                pending=std::move(*snapshot);changed=true;
            }
            if(decoded.valid() && decoded.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                videoBody=decoded.get();changed=true;
            }
            if(!decoded.valid() && pending) {
                const auto dropped=mailbox.dropped()+decodeDrops;
                decoded=std::async(std::launch::async,[&inspector,snapshot=std::move(*pending),dropped] {
                    auto view=inspector.decode(snapshot,dropped);
                    view.erase("cpu");view.erase("sequence");view.erase("cycles");view.erase("droppedSnapshots");
                    view["videoSequence"]=std::to_string(snapshot.sequence);
                    view["videoCycles"]=std::to_string(snapshot.cycles);
                    view["videoGeneration"]=std::to_string(snapshot.cpu.generation);
                    view["videoPauseId"]=std::to_string(snapshot.cpu.pauseId);
                    auto text=view.dump();text.pop_back();return text;
                });
                pending.reset();
            }
            // CPU replies and video views are independently owned observations.
            // Their separate identities expose video age instead of delaying
            // controls or implying that an older frame is current guest state.
            if(changed && !videoBody.empty())state=videoBody+",\"cpu\":"+cpuBody+
                ",\"sequence\":\""+std::to_string(latestSequence)+"\",\"cycles\":\""+std::to_string(latestCycles)+
                "\",\"droppedSnapshots\":"+std::to_string(mailbox.dropped()+decodeDrops)+"}";
            // ID-zero retirement notifications are represented by snapshots.
            while(auto reply=connection.service->response())server.deliver(*reply);
            meta["transportRejected"]=server.rejected();meta["transportDisconnected"]=server.disconnected();
            server.tick(state,meta.dump(),[&](const Debug::Command& command){return connection.service->request(command);});
        }
        machineLane.request_stop();machineLane.join();
        if(failed.load())throw std::runtime_error("machine inspection lane failed");
        return 0;
    }catch(const std::exception& e){std::cerr<<"time-browser: "<<e.what()<<'\n';return 2;}
}
