#include "space/Session.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/mappers/CodemastersMapper.hpp"
#include <chrono>
#include <iostream>

using namespace BMMQ;
using namespace BMMQ::Space;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void reject(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"expected rejection");}
Json effects(Capture& c){Json out=Json::array();Record r;while(c.pop(r)){
    if(r.kind==Kind::Read||r.kind==Kind::Write||r.kind==Kind::Device||r.kind==Kind::Dma||r.kind==Kind::Mapping)
        out.push_back({unsigned(r.kind),r.address,r.location,r.value,r.accepted,r.isWrite,unsigned(r.origin)});
}return out;}
template<class M>struct Pair {
    M baseline,snapshot;
    Capture baselineTrace,snapshotTrace;
    Execution execution{snapshot,snapshotTrace};
    explicit Pair(const std::vector<uint8_t>& rom){
        baseline.loadRom(rom);snapshot.loadRom(rom);
        for(auto* machine:{&baseline,&snapshot}){
            machine->runtimeContext().writeRegister16("PC",0x200);
            machine->runtimeContext().writeRegister16("SP",0xcff0);
        }
        baseline.setAnalysisCapture(&baselineTrace);snapshot.setAnalysisCapture(&snapshotTrace);
        snapshot.setSnapshotExecution(&execution);execution.mode("snapshot");
    }
    ~Pair(){baseline.setAnalysisCapture(nullptr);snapshot.setAnalysisCapture(nullptr);}
    void step(unsigned count=1){while(count--){baseline.step();snapshot.step();
        require(baseline.deterministicStateFingerprint()==snapshot.deterministicStateFingerprint(),"instruction register/memory/cycle/device/mapper parity");
        require(effects(baselineTrace)==effects(snapshotTrace),"ordered effects differ");}}
};
std::vector<uint8_t> image(std::span<const uint8_t> code,uint8_t type=0,uint8_t ram=0,bool codemasters=false){
    std::vector<uint8_t> rom(65536,0);rom[0x147]=type;rom[0x149]=ram;
    if(codemasters){const std::string name="CODEMASTERS";std::copy(name.begin(),name.end(),rom.begin()+0x20);}
    std::copy(code.begin(),code.end(),rom.begin()+0x200);return rom;
}
void legacy(Machine& machine,Capture& capture,Execution& current,const std::string& core){
    auto saved=current.state();saved["schemaVersion"]=core=="gameboy"?1:2;saved.erase("ramCapacity");
    saved["executionBytes"]=decimal(3u*1024u*1024u+saved["blocks"].size()*512u*1024u);
    for(auto& block:saved["blocks"])for(auto& key:block["instructions"])key.erase("fetchDigest");
    Execution loaded(machine,capture);loaded.restore(saved,false);require(loaded.state()["schemaVersion"]==3,"legacy execution did not upgrade");
    require(loaded.state()["values"]==current.state()["values"],"legacy suppliers changed");
}
}
int main(){try{
    const std::vector<uint8_t> gbBanks={
        0x3e,0x0a,0xea,0,0, 0x3e,0,0xea,0,0x40,
        0x3e,0x11,0xea,0,0xa0,0xfa,0,0xa0,0xea,0,0xc0,
        0x3e,1,0xea,0,0x40,0x3e,0x22,0xea,0,0xa0,0xfa,0,0xa0,0xea,1,0xc0,
        0x3e,0,0xea,0,0x40,0xfa,0,0xa0,0xea,2,0xc0,
        0x3e,0,0xea,0,0,0xfa,0,0xa0,0xea,3,0xc0,0x18,0xfe};
    for(auto type:{uint8_t(3),uint8_t(0x13),uint8_t(0x1b)}){
        auto code=gbBanks;if(type==3)code.insert(code.begin(),{0x3e,1,0xea,0,0x60});
        Pair<GB::GameBoyMachine> p(image(code,type,3));p.step(30);
        auto core=makeCoreAdapter(p.snapshot);require(core->rawRead(0xc000)==0x11&&core->rawRead(0xc001)==0x22&&core->rawRead(0xc002)==0x11&&core->rawRead(0xc003)==0xff,"GB bank/RAM-disable observations");
        auto state=p.execution.state();require(state["values"]["65536"]["value"]==0x11&&state["values"]["73728"]["value"]==0x22,"GB banks shared a supplier");
        // Physical suppliers verify correctly even with cartridge RAM disabled.
        Execution restored(p.snapshot,p.snapshotTrace);restored.restore(state,false);
        require(restored.state()["values"]==state["values"],"disabled GB banks lost on restore");
        auto corrupt=state;corrupt["values"]["196608"]=state["values"]["65536"];reject([&]{restored.restore(corrupt,false);});
    }
    const std::vector<uint8_t> nibble={0x3e,0x0a,0xea,0,0,0x3e,0xab,0xea,0,0xa2,0xfa,0,0xa0,0xea,0,0xc0,
        0x3e,4,0xea,0xff,0xa1,0xfa,0xff,0xa3,0xea,1,0xc0,0x18,0xfe};
    {Pair<GB::GameBoyMachine> p(image(nibble,6));p.step(16);auto core=makeCoreAdapter(p.snapshot);
        require(core->rawRead(0xc000)==0xfb&&core->rawRead(0xc001)==0xf4,"MBC2 mirrored nibble reads differ");
        require(core->location(0xa000)==core->location(0xa200)&&core->location(0xa1ff)==core->location(0xa3ff),"MBC2 aliases have different physical identities");
        require(p.execution.state()["values"]["65536"]["value"]==0xfb,"MBC2 supplier omitted fixed read bits");}
    const std::vector<uint8_t> rtc={0x3e,0x0a,0xea,0,0,0x3e,8,0xea,0,0x40,0x3e,5,0xea,0,0xa0,0xfa,0,0xa0,0xea,0,0xc0,0x18,0xfe};
    {Pair<GB::GameBoyMachine> p(image(rtc,0x10,3));p.step(14);require(counter(p.execution.status()["busReads"])>0,"RTC read claimed sparse RAM execution");require(!p.execution.state()["values"].contains("65536"),"RTC stored as RAM supplier");}
    const std::vector<uint8_t> ggBanks={
        0x3e,8,0x32,0xfc,0xff,0x3e,0x11,0x32,0,0x80,0x3a,0,0x80,0x32,0,0xc0,
        0x3e,12,0x32,0xfc,0xff,0x3e,0x22,0x32,0,0x80,0x3a,0,0x80,0x32,1,0xc0,
        0x3e,8,0x32,0xfc,0xff,0x3a,0,0x80,0x32,2,0xc0,
        0x3a,0x10,0xff,0x32,3,0xc0,0x18,0xfe};
    {Pair<GameGearMachine> p(image(ggBanks));p.step(24);auto core=makeCoreAdapter(p.snapshot);
        require(core->rawRead(0xc000)==0x11&&core->rawRead(0xc001)==0x22&&core->rawRead(0xc002)==0x11,"GG SRAM bank values differ");
        auto state=p.execution.state();require(state["values"]["65536"]["value"]==0x11&&state["values"]["81920"]["value"]==0x22,"GG SRAM banks shared a supplier");
        require(counter(p.execution.status()["busReads"])>0,"compatibility audio read bypassed authoritative bus");
        Execution restored(p.snapshot,p.snapshotTrace);restored.restore(state,false);require(restored.state()["values"]==state["values"],"GG unmapped bank lost on restore");}
    // FFFC's mapper-control write also changes the work RAM at DFFC. An
    // existing sparse supplier must publish that mirror write exactly once.
    const std::vector<uint8_t> controlMirror={0x3e,0x55,0x32,0xfc,0xdf,0x3a,0xfc,0xdf,
        0x3e,8,0x32,0xfc,0xff,0x3a,0xfc,0xdf,0x32,0,0xc0,0x18,0xfe};
    {Pair<GameGearMachine> p(image(controlMirror));p.step(4);const auto before=counter(p.execution.state()["sequence"]);p.step();
        require(counter(p.execution.state()["values"]["57340"]["sequence"])==before+1,"mirror write published more than once");
        p.step(7);auto core=makeCoreAdapter(p.snapshot);
        require(core->rawRead(0xc000)==8&&p.execution.state()["values"]["57340"]["value"]==8,"mapper control left stale work-RAM mirror supplier");
    }
    const std::vector<uint8_t> cmCode={0x3e,0x81,0x32,0,0x40,0x3e,0x5a,0x32,0,0xa0,0x3a,0,0xa0,0x32,0,0xc0,
        0x3e,0x3c,0x32,0xff,0xbf,0x3e,1,0x32,0,0x80,0x3a,0,0xa0,0x32,1,0xc0,
        0x3e,1,0x32,0,0x40,0x3e,0x81,0x32,0,0x40,0x3a,0xff,0xbf,0x32,2,0xc0,0x18,0xfe};
    const auto cmRom=image(cmCode,0,0,true);
    {Pair<GameGearMachine> p(cmRom);p.step(28);auto core=makeCoreAdapter(p.snapshot);
        require(core->rawRead(0xc000)==0x5a&&core->rawRead(0xc001)==0x5a&&core->rawRead(0xc002)==0x3c,"Codemasters mapped writes/control precedence");
        require(core->location(0xa000)==location(3,3,0),"Codemasters physical RAM identity");
        require(p.execution.state()["values"]["98304"]["value"]==0x5a,"Codemasters RAM not sparse supplier");}
    // A fresh machine has not allocated Codemasters extra RAM yet. Coordinated
    // restore must validate on its disposable candidate, then publish atomically.
    auto directory=std::filesystem::temp_directory_path()/("space-cart-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{directory};
    GameGearMachine original,fresh;original.loadRom(cmRom);fresh.loadRom(cmRom);
    original.runtimeContext().writeRegister16("PC",0x200);fresh.runtimeContext().writeRegister16("PC",0x200);
    Session recorded(original,cmRom),restored(fresh,cmRom);recorded.executionMode("snapshot");restored.executionMode("snapshot");
    for(unsigned i=0;i<28;++i)recorded.step();recorded.checkpoint(directory/"cm");restored.restore(directory/"cm");
    require(restored.fingerprint()==recorded.fingerprint(),"Codemasters coordinated restore into unallocated RAM");
    for(unsigned i=0;i<4;++i){recorded.step();restored.step();require(restored.fingerprint()==recorded.fingerprint(),"Codemasters restored execution parity");}
    const std::vector<uint8_t> work={0,0x18,0xfd};
    {Pair<GB::GameBoyMachine> p(image(work));p.step(4);legacy(p.snapshot,p.snapshotTrace,p.execution,"gameboy");}
    {Pair<GameGearMachine> p(image(work));p.step(4);legacy(p.snapshot,p.snapshotTrace,p.execution,"gamegear");}
    // Identical bytes straddling a ROM window have different ownership when
    // their immediate operand is fetched from a different physical bank.
    auto boundaryRom=image(work,0x1b,3);boundaryRom[0x3fff]=0x3e;boundaryRom[0x4000]=0x44;boundaryRom[0x8000]=0x44;
    {Pair<GB::GameBoyMachine> p(boundaryRom);
        for(auto* m:{&p.baseline,&p.snapshot})m->runtimeContext().writeRegister16("PC",0x3fff);p.step();
        for(auto* m:{&p.baseline,&p.snapshot}){m->runtimeContext().write8(0x2000,2);m->runtimeContext().writeRegister16("PC",0x3fff);}p.step();
        const auto state=p.execution.state();unsigned versions=0;for(auto& block:state["blocks"])for(auto& key:block["instructions"])
            if(key["location"]==decimal(location(1,0,0x3fff)))++versions;
        require(versions==2,"GB bank-boundary fetch identity aliased");}
    {Pair<GameGearMachine> p(boundaryRom);
        for(auto* m:{&p.baseline,&p.snapshot})m->runtimeContext().writeRegister16("PC",0x3fff);p.step();
        for(auto* m:{&p.baseline,&p.snapshot}){m->runtimeContext().write8(0xfffe,2);m->runtimeContext().writeRegister16("PC",0x3fff);}p.step();
        const auto state=p.execution.state();unsigned versions=0;for(auto& block:state["blocks"])for(auto& key:block["instructions"])
            if(key["location"]==decimal(location(1,0,0x3fff)))++versions;
        require(versions==2,"GG bank-boundary fetch identity aliased");}
    for(const auto coreName:{std::string("gameboy"),std::string("gamegear")}){
        std::unique_ptr<Machine> machine=coreName=="gameboy"?std::unique_ptr<Machine>(std::make_unique<GB::GameBoyMachine>()):std::make_unique<GameGearMachine>();
        machine->loadRom(boundaryRom);machine->runtimeContext().writeRegister16("PC",0x3fff);
        Session session(*machine,boundaryRom);session.step();
        machine->runtimeContext().write8(coreName=="gameboy"?0x2000:0xfffe,2);machine->runtimeContext().writeRegister16("PC",0x3fff);session.step();
        const auto doc=session.document();Project::validate(doc);unsigned versions=0;std::string bankOne;
        for(auto& instruction:doc["instructions"])if(instruction["location"]==decimal(location(1,0,0x3fff))){
            require(instruction.contains("fetchBackings")&&instruction["fetchBackings"].size()==2,"bank-boundary fetch evidence missing");++versions;}
        require(versions==2,"analysis merged physically distinct bank-boundary fetches");
        for(auto it=doc["instructions"].begin();it!=doc["instructions"].end();++it)
            if(it.value()["fetchBackings"][1]["location"]==decimal(location(1,1,0)))bankOne=it.key();
        require(!bankOne.empty(),"missing bank-one target");session.analyze();
        machine->runtimeContext().writeRegister16("PC",0x3fff);
        require(session.runUntil({{"instruction",bankOne},{"count",1}})["reason"]=="step_limit","exploration matched identical bytes from the wrong physical bank");
    }
    const std::vector<uint8_t> haltBug={0xf3,0x3e,1,0xea,0xff,0xff,0xe0,0x0f,0x76,0x3e,0x42,0x18,0xfe};
    {const auto rom=image(haltBug);GB::GameBoyMachine baseline,snapshot;baseline.loadRom(rom);snapshot.loadRom(rom);
        baseline.runtimeContext().writeRegister16("PC",0x200);snapshot.runtimeContext().writeRegister16("PC",0x200);
        Session b(baseline,rom),s(snapshot,rom);s.executionMode("snapshot");
        for(unsigned i=0;i<16;++i){b.step();s.step();require(b.fingerprint()==s.fingerprint(),"HALT duplicate-address operand fetch parity");}
        const auto doc=s.document();Project::validate(doc);bool duplicate=false;
        for(auto& n:doc["instructions"])if(n["bytes"]=="3e3e"){
            require(n["fetchBackings"].size()==2&&n["fetchBackings"][0]["location"]==n["fetchBackings"][1]["location"],"HALT bug fetch backing was normalized away");duplicate=true;}
        require(duplicate,"HALT bug fixture did not execute duplicate fetch");}
    {Pair<GB::GameBoyMachine> p(image(work,0x1b,3));
        for(auto* m:{&p.baseline,&p.snapshot}){
            m->runtimeContext().write8(0,0x0a);m->runtimeContext().write8(0x4000,1);
            m->runtimeContext().write8(0xa000,0);m->runtimeContext().writeRegister16("PC",0xa000);
        }
        p.step();for(auto* m:{&p.baseline,&p.snapshot}){m->runtimeContext().write8(0xa000,0x3c);m->runtimeContext().writeRegister16("PC",0xa000);}p.step();
        const auto state=p.execution.state();unsigned versions=0;for(auto& b:state["blocks"])for(auto& k:b["instructions"])if(k["location"]==decimal(location(3,1,0)))++versions;
        require(versions==2,"rewritten GB cartridge RAM code lost instruction identity");}
    {Pair<GameGearMachine> p(image(work));
        for(auto* m:{&p.baseline,&p.snapshot}){m->runtimeContext().write8(0xfffc,12);m->runtimeContext().write8(0x8000,0);m->runtimeContext().writeRegister16("PC",0x8000);}
        p.step();for(auto* m:{&p.baseline,&p.snapshot}){m->runtimeContext().write8(0x8000,0x3c);m->runtimeContext().writeRegister16("PC",0x8000);}p.step();
        const auto state=p.execution.state();unsigned versions=0;for(auto& b:state["blocks"])for(auto& k:b["instructions"])if(k["location"]==decimal(location(3,2,0)))++versions;
        require(versions==2,"rewritten GG cartridge RAM code lost instruction identity");}
    std::cout<<"physical cartridge RAM suppliers, aliases, device authority and coordinated/legacy checkpoints passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
