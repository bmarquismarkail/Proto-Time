#include "space/Session.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace BMMQ;
using namespace BMMQ::Space;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F>void reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"expected rejection");}
Json effects(Capture& c){Json result=Json::array();Record r;while(c.pop(r)){if(r.kind==Kind::Read||r.kind==Kind::Write||r.kind==Kind::Mapping||r.kind==Kind::Device)
    result.push_back({unsigned(r.kind),r.address,r.value,r.isWrite,r.accepted,r.location,unsigned(r.origin)});}return result;}
struct Pair {
    GameGearMachine baseline,snapshot;
    Capture btrace,strace;
    Execution execution{snapshot,strace};
    explicit Pair(const std::vector<uint8_t>& rom){
        baseline.loadRom(rom);snapshot.loadRom(rom);
        for(auto* machine:{&baseline,&snapshot}){
            for(auto name:{"AF","BC","DE","HL","IX","IY"})machine->runtimeContext().writeRegister16(name,std::string_view(name)=="AF"?0x5a95:0xc100);
            machine->runtimeContext().writeRegister16("SP",0xcf00);
            machine->runtimeContext().writeRegister16("PC",0x200);
            for(unsigned i=0;i<64;++i)machine->runtimeContext().write8(uint16_t(0xc0f0+i),uint8_t(i*13));
            machine->runtimeContext().write8(0xcf00,0);machine->runtimeContext().write8(0xcf01,2);
        }
        baseline.setAnalysisCapture(&btrace);snapshot.setAnalysisCapture(&strace);snapshot.setSnapshotExecution(&execution);execution.mode("snapshot");
    }
    ~Pair(){baseline.setAnalysisCapture(nullptr);snapshot.setAnalysisCapture(nullptr);}
    void step(const std::string& description){
        baseline.step();snapshot.step();
        require(baseline.deterministicStateFingerprint()==snapshot.deterministicStateFingerprint(),"state/cycle parity: "+description);
        require(effects(btrace)==effects(strace),"ordered memory/port/device effects: "+description);
    }
};
std::vector<uint8_t> rom(std::span<const uint8_t> code){std::vector<uint8_t> r(32768,0);std::copy(code.begin(),code.end(),r.begin()+0x200);return r;}
}
int main(){try{
    // Absolute loads consume both address bytes, with any ignored/index prefix
    // bytes retained in the instruction identity. Verify the real snapshot and
    // capture paths as well as the shared decoder; state parity alone previously
    // missed a truncated pre-execution identity for 2A/3A.
    for(auto prefix:std::vector<std::vector<uint8_t>>{{},{0xdd},{0xfd},{0xdd,0xfd}})
        for(auto op:{uint8_t(0x2a),uint8_t(0x3a)}){
            auto bytes=prefix;bytes.insert(bytes.end(),{op,0x10,0xc0});
            require(z80InstructionLength(bytes)==bytes.size(),"absolute load encoded length");
            const auto image=rom(bytes);GameGearMachine machine;machine.loadRom(image);
            machine.runtimeContext().writeRegister16("PC",0x200);
            const auto fetched=machine.snapshotInstruction();
            require(std::vector<uint8_t>(fetched.begin(),fetched.end())==bytes,"absolute load snapshot identity");
            Session session(machine,image);session.executionMode("snapshot");session.step();
            const auto captured=session.document();
            require(captured["instructions"].size()==1&&captured["instructions"].begin().value()["length"]==bytes.size(),
                "absolute load captured length");
        }
    // All base, CB, ED, DD, FD, DD-CB and FD-CB byte families. No NDEBUG assertions.
    for(unsigned family=0;family<7;++family)for(unsigned op=0;op<256;++op){
        std::vector<uint8_t> code;
        if(family==1)code={0xcb};
        if(family==2)code={0xed};
        if(family==3)code={0xdd};
        if(family==4)code={0xfd};
        if(family==5)code={0xdd,0xcb,7};
        if(family==6)code={0xfd,0xcb,7};
        code.push_back(uint8_t(op));code.insert(code.end(),{0x34,0xc0,0});
        Pair pair(rom(code));pair.step(std::to_string(family)+":"+std::to_string(op));
    }
    std::vector<uint8_t> code={0x21,0x00,0xc0,0x36,0x07,0x7e,0xdd,0x21,0x10,0xc0,
        0xdd,0x36,0x03,0x21,0xdd,0x7e,0x03,0x32,0x20,0xe0,0x3a,0x20,0xc0,
        0x08,0x3e,0x44,0x08,0xd9,0x01,0x34,0x12,0xd9,0x3e,0x80,0xd3,0x7f,
        0x3e,0,0xd3,0xbf,0x3e,0x40,0xd3,0xbf,0x3e,0x5a,0xd3,0xbe,0xdb,0xdc,
        0xc3,0x00,0x02};
    auto image=rom(code);GameGearMachine b,s;b.loadRom(image);s.loadRom(image);
    b.runtimeContext().writeRegister16("PC",0x200);s.runtimeContext().writeRegister16("PC",0x200);
    Session baseline(b,image),snapshot(s,image);snapshot.executionMode("snapshot");baseline.input(1);snapshot.input(1);
    for(unsigned i=0;i<80;++i){baseline.step();snapshot.step();require(baseline.fingerprint()==snapshot.fingerprint(),"fixture parity");}
    auto doc=snapshot.document();Project::validate(doc);require(doc["core"]=="gamegear"&&doc["schemaVersion"]==2,"core-aware project");require(doc["gaps"].empty(),"unexpected capture gap");
    bool indexed=false,ports=false;
    for(auto it=doc["instructions"].begin();it!=doc["instructions"].end();++it){indexed=indexed||it.value()["length"].get<unsigned>()==4;require(it.value()["registers"].contains("IX")&&it.value()["registers"].contains("A'")&&it.value()["registers"].contains("IFF2"),"incomplete register view");}
    for(auto& d:doc["dependencies"])ports=ports||d["location"].get<std::string>().starts_with("m:"+decimal(location(7,0,0xdc)));
    require(indexed&&ports,"missing indexed/port evidence");
    auto analyzed=analyzeProject(doc);require(analyzed==analyzeProject(doc),"nondeterministic Game Gear analysis");Project::validate(analyzed);
    require(!analyzed["analysis"]["dataTransfers"].empty(),"Game Gear load-copy provenance missing");
    auto pollingRom=rom(std::array<uint8_t,4>{0xdb,0xdc,0x18,0xfc});
    GameGearMachine pollingMachine;pollingMachine.loadRom(pollingRom);pollingMachine.runtimeContext().writeRegister16("PC",0x200);
    Session pollingSession(pollingMachine,pollingRom);for(unsigned i=0;i<8;++i)pollingSession.step();
    auto pollingAnalysis=analyzeProject(pollingSession.document());bool polling=false;
    for(auto& finding:pollingAnalysis["analysis"]["findings"])polling=polling||finding["role"]=="input.polling";
    require(polling,"Game Gear input polling evidence missing");
    auto path=std::filesystem::temp_directory_path()/("time-space-gg-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(path);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{path};
    snapshot.checkpoint(path/"paired");for(unsigned i=0;i<12;++i)snapshot.step();auto replay=snapshot.fingerprint();
    snapshot.restore(path/"paired");for(unsigned i=0;i<12;++i)snapshot.step();require(replay==snapshot.fingerprint(),"Game Gear checkpoint replay");
    auto before=snapshot.fingerprint();auto manifest=Project::read(path/"paired/manifest.json");manifest["core"]="gameboy";Project::write(path/"paired/manifest.json",manifest);
    reject([&]{snapshot.restore(path/"paired");});require(before==snapshot.fingerprint(),"cross-core checkpoint mutated machine");
    reject([&]{s.loadRom(image);});reject([&]{s.loadExternalBootRom({0});});
    auto incompatible=Project(digest(image)).document();
    Project project(digest(image),Project::defaultBudget,"gamegear");reject([&]{project.merge(incompatible);});
    // Chunked capture retains long prefix strings and exact fetch identity.
    std::vector<uint8_t> longCode(201,0xdd);longCode.back()=0;auto longRom=rom(longCode);
    Pair longPair(longRom);longPair.step("long prefixes");
    GameGearMachine longMachine;longMachine.loadRom(longRom);longMachine.runtimeContext().writeRegister16("PC",0x200);Session longSession(longMachine,longRom);longSession.step();
    auto longDoc=longSession.document();require(longDoc["instructions"].size()==1,"prefix instruction split");require(longDoc["instructions"].begin().value()["length"]==201,"prefix truncation");Project::validate(longDoc);
    s.setAnalysisCapture(nullptr);require(!snapshot.executionStatus()["activeMode"].get<std::string>().empty(),"capture detach status");
    std::cout<<"Game Gear SPACE opcode families, effects, provenance and paired checkpoints passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
