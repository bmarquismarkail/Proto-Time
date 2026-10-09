#include "machine/plugins/video/remote/Inspector.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/CoreAdapter.hpp"
#include <iostream>
#include <thread>
using namespace BMMQ;using namespace BMMQ::RemoteVideo;using Json=nlohmann::json;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F>void reject(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}require(caught,"invalid inspection request accepted");}
Snapshot capture(Machine& machine,Debug::DebugService& debug) {
    auto frame=machine.realtimeVideoPacket({160,144});auto video=machine.videoStateSnapshot();
    require(frame&&video,"missing capture capability");return {debug.currentSnapshot(),std::move(frame->packet),std::move(*video),1,0};
}
void verify(Machine& machine,const std::string& core) {
    std::vector<std::uint8_t> rom(32768,0);
    if(core=="gameboy") {rom[0x100]=0x18;rom[0x101]=0xfe;}
    else {const std::array<std::uint8_t,17> code={0x3e,0,0xd3,0xbf,0x3e,0x40,0xd3,0xbf,0x3e,0xff,0xd3,0xbe,0x18,0xfe};std::copy(code.begin(),code.end(),rom.begin());}
    machine.loadRom(rom);
    if(core=="gamegear")for(unsigned i=0;i<6;++i)machine.step();
    else {machine.runtimeContext().write8(0xff40,0);machine.runtimeContext().write8(0x8000,0xff);}
    auto adapter=Space::makeCoreAdapter(machine);
    const auto before=adapter->fingerprint();
    Debug::DebugService debug(machine,false);Inspector inspector(core);auto owned=capture(machine,debug);
    require(adapter->fingerprint()==before,"inspection mutated guest");
    auto view=inspector.decode(owned,9);
    require(view["core"]==core&&view["droppedSnapshots"]==9,"inspection metadata");
    require(view["frame"]["rgba"].get<std::string>().size()==160*144*8,"frame extent");
    require(view["atlas"]["tileCount"]==(core=="gameboy"?384:512),"tile count");
    require(view["atlas"]["rgba"].get<std::string>().size()==(core=="gameboy"?384:512)*64*8,"atlas extent");
    require(view["semantics"].size()==23040&&view["vram"].size()==(core=="gameboy"?8192:16384),"video extent");
    if(core=="gamegear")require(view["sprites"].size()==64&&view["sprites"][0].size()==3,"VDP SAT coverage");
    const auto atlas=view["atlas"]["rgba"].get<std::string>();
    require(owned.video.vram[0]==0xff,"real pattern fixture not written");
    require(atlas.substr(0,8)==(core=="gameboy"?"081820ff":view["paletteRgba"].get<std::string>().substr(8,8)),"pattern planes/palette mapping");
    if(core=="gamegear")require(view["atlas"]["rgbaSprite"].get<std::string>().substr(0,8)==view["paletteRgba"].get<std::string>().substr(17*8,8),"sprite palette selected by color instead of index");
    const auto original=view;
    machine.runtimeContext().write8(0xc000,0x55);
    require(inspector.decode(owned,9)==original,"owned view changed after guest write");
    auto invalid=owned;invalid.video.machineId="unknown";reject([&]{inspector.decode(invalid,0);});
    invalid=owned;invalid.frame.width=256;reject([&]{inspector.decode(invalid,0);});
    invalid=owned;invalid.video.vram.pop_back();reject([&]{inspector.decode(invalid,0);});
    auto command=Inspector::command({{"operation","step"},{"generation",std::to_string(owned.cpu.generation)},
        {"pauseId",std::to_string(owned.cpu.pauseId)},{"steps",10000}},1);
    require(debug.request(command),"command enqueue");debug.run(10000);
    auto response=debug.response();require(response&&response->error==Debug::Error::None,"step accepted");
    require(debug.currentSnapshot().lost==0&&!debug.trace(),"inspection-only lease collected lost trace");
    Json bad{{"operation","inspect"},{"generation","1"},{"pauseId","1"},{"address",65535},{"length",2}};
    reject([&]{Inspector::command(bad,1);});bad["address"]=-1;reject([&]{Inspector::command(bad,1);});
    bad["address"]=0;bad["generation"]="18446744073709551616";reject([&]{Inspector::command(bad,1);});
    bad["generation"]="1";bad["pauseId"]=1;reject([&]{Inspector::command(bad,1);});
    Json edit{{"operation","edit"},{"generation","1"},{"pauseId","1"},{"bytes",Json::array({{{"address",0xc000},{"value",256}}})}};
    reject([&]{Inspector::command(edit,1);});
}
}
int main(){try{
    GB::GameBoyMachine gb;GameGearMachine gg;verify(gb,"gameboy");verify(gg,"gamegear");
    reject([]{Inspector("unknown");});
    // Concurrent bounded handoff: checks storage ownership, monotonic samples,
    // final visibility and explicit overwrite accounting under real contention.
    Mailbox mailbox;std::atomic<bool> done{false};
    auto producer=[&] {for(unsigned i=1;i<=400;++i){Snapshot s;s.sequence=i;s.video.vram.assign(8192,std::uint8_t(i));
        s.frame.width=160;s.frame.height=144;s.frame.surface.encoding=RealtimeVideoEncoding::Argb8888;s.frame.surface.argbPixels.assign(23040,0xff000000u|i);
        if(!mailbox.publish(std::move(s)))std::terminate();}done.store(true,std::memory_order_release);};
    std::jthread worker(producer);std::uint64_t last=0,consumed=0;
    while(!done.load(std::memory_order_acquire)||last<400)if(auto s=mailbox.consume()) {
        require(s->sequence>last,"snapshot order reversed");require(s->video.vram[0]==std::uint8_t(s->sequence)&&s->frame.surface.argbPixels[0]==(0xff000000u|s->sequence),"borrowed/torn snapshot");last=s->sequence;++consumed;
    }
    worker.join();require(consumed+mailbox.dropped()==400,"handoff accounting");
    std::cout<<"Browser owned inspection, video memory, controls and bounded handoff passed on both cores\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
