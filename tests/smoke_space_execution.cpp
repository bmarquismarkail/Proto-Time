#include "space/Session.hpp"
#include "fixtures/space/Fixture.hpp"
#include <iostream>
#include <chrono>
using namespace BMMQ::Space;
namespace {
void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"expected rejection");}
struct Pair {
    GB::GameBoyMachine baseline,snapshot;
    Capture baselineTrace,snapshotTrace;
    Execution execution{snapshot,snapshotTrace};
    Pair(const std::vector<uint8_t>& rom){baseline.loadRom(rom);snapshot.loadRom(rom);
        baseline.setAnalysisCapture(&baselineTrace);snapshot.setAnalysisCapture(&snapshotTrace);snapshot.setSnapshotExecution(&execution);execution.mode("snapshot");}
    ~Pair(){baseline.setAnalysisCapture(nullptr);snapshot.setSnapshotExecution(nullptr);snapshot.setAnalysisCapture(nullptr);}
    static Json effects(Capture& capture){Json result=Json::array();Record r;while(capture.pop(r)){
        if(r.kind==Kind::Read||r.kind==Kind::Write||r.kind==Kind::Device||r.kind==Kind::Dma||r.kind==Kind::Mapping)
            result.push_back({static_cast<unsigned>(r.kind),r.address,r.value,r.isWrite,r.accepted,decimal(r.location)});
        }
        return result;}
    void step(){baseline.step();snapshot.step();
        check(baseline.deterministicStateFingerprint()==snapshot.deterministicStateFingerprint(),"instruction state/cycle parity failed");
        check(effects(baselineTrace)==effects(snapshotTrace),"ordered bus/device effects differ");}
};
}
int main(){try{
    auto rom=spaceFixture();
    // Capture detachment clears both borrowed controller pointers, including a paused one.
    {
        GB::GameBoyMachine baseline,detached;baseline.loadRom(rom);detached.loadRom(rom);
        Capture capture,replacement;
        auto controller=std::make_unique<Execution>(detached,capture);
        detached.setAnalysisCapture(&capture);detached.setSnapshotExecution(controller.get());controller->mode("snapshot");
        for(int i=0;i<8;++i){baseline.step();detached.step();}
        check(baseline.deterministicStateFingerprint()==detached.deterministicStateFingerprint(),"detach fixture state mismatch");
        capture.stop();controller->evidenceLost();
        auto fingerprint=detached.deterministicStateFingerprint();rejects([&]{detached.step();});
        check(detached.deterministicStateFingerprint()==fingerprint,"paused controller changed guest state");
        detached.setAnalysisCapture(nullptr);
        auto& map=dynamic_cast<GB::GameBoyMemoryMap&>(detached.executionMemory().backingStore());
        check(map.analysisCapture==nullptr&&map.snapshotExecution==nullptr,"capture detach retained memory controller");
        auto status=controller->status();
        rejects([&]{detached.setSnapshotExecution(controller.get());});
        for(int i=0;i<8;++i){baseline.step();detached.step();}
        check(controller->status()==status,"detached controller received execution callbacks");
        detached.setAnalysisCapture(&replacement);
        check(map.snapshotExecution==nullptr,"new capture reattached an old controller");
        for(int i=0;i<8;++i){baseline.step();detached.step();}
        check(controller->status()==status,"replacement capture used a stale controller");
        detached.setAnalysisCapture(nullptr);detached.setAnalysisCapture(nullptr);controller.reset();
        for(int i=0;i<8;++i){baseline.step();detached.step();}
        check(baseline.deterministicStateFingerprint()==detached.deterministicStateFingerprint(),"execution after controller destruction changed state/cycles");
    }
    Pair loop(rom);for(int i=0;i<48;++i)loop.step();
    check(counter(loop.execution.status()["snapshotReads"])>0,"RAM did not execute from snapshots");
    auto history=loop.execution.state();check(history["values"]["49152"]["value"]==3,"latest writer value lost");
    // An unchanged write remains a writer, while a read does not replace that supplier.
    uint64_t prior=counter(history["values"]["49152"]["sequence"]);bool seenRead=false,seenUnchangedWriter=false;
    for(int i=0;i<8;++i){loop.step();auto now=loop.execution.state();auto seq=counter(now["values"]["49152"]["sequence"]);
        if(seq==prior)seenRead=true;
        else if(now["values"]["49152"]["value"]==3)seenUnchangedWriter=true;
        prior=seq;}
    check(seenRead,"reads acquired write ownership");
    check(seenUnchangedWriter,"unchanged write did not become the latest writer");
    // A's stale local value must refresh from B on a genuine A -> B -> A entry.
    auto abaRom=rom;
    const uint8_t aCode[]={0x21,0,0xc0,0x7e,0x3c,0x77,0xc3,0x70,1};
    const uint8_t bCode[]={0x3e,7,0xea,0,0xc0,0xc3,0x50,1};
    std::copy(std::begin(aCode),std::end(aCode),abaRom.begin()+0x150);
    std::copy(std::begin(bCode),std::end(bCode),abaRom.begin()+0x170);
    Pair aba(abaRom);for(int i=0;i<5;++i)aba.step();
    auto aWriter=aba.execution.state()["values"]["49152"]["block"];
    for(int i=0;i<3;++i)aba.step();
    auto bSupplier=aba.execution.state()["values"]["49152"];
    check(bSupplier["block"]!=aWriter&&bSupplier["value"]==7,"B did not replace A's supplier");
    for(int i=0;i<3;++i)aba.step();
    auto refreshed=aba.execution.state();
    check(refreshed["activeBlock"]==aWriter&&refreshed["values"]["49152"]==bSupplier,"A re-entry changed supplier ownership");
    auto aIndex=counter(aWriter)-1;
    check(refreshed["blocks"][aIndex]["bytes"]["49152"]==7,"A retained stale local bytes after B wrote");
    // Exercise every supported base opcode and every CB operation through both adapters.
    unsigned supported=0;
    for(unsigned op=0;op<256;++op){if(!loop.baseline.snapshotOpcodeSupported(op))continue;
        auto program=rom;program[0x150]=op;program[0x151]=1;program[0x152]=0xc0;Pair pair(program);
        pair.baseline.executionMemory().file.findRegister("PC")->reg->value=0x150;
        pair.snapshot.executionMemory().file.findRegister("PC")->reg->value=0x150;
        pair.step();++supported;}
    for(unsigned op=0;op<256;++op){auto program=rom;program[0x150]=0xcb;program[0x151]=op;Pair pair(program);
        for(auto* machine:{&pair.baseline,&pair.snapshot}){auto& file=machine->executionMemory().file;file.findRegister("PC")->reg->value=0x150;file.findRegister("HL")->reg->value=0xc000;}
        pair.step();}
    check(supported>200,"opcode differential coverage incomplete");
    // CPU aliases, HRAM stack, echo RAM and authoritative MMIO.
    auto hardware=rom;const uint8_t program[]={0x31,0xfe,0xff,0x3e,0x51,0xf5,0xc1,0xea,0x00,0xe0,0xfa,0x00,0xc0,0xe0,0x04,0xf0,0x04,0xfb,0,0x76};
    std::copy(std::begin(program),std::end(program),hardware.begin()+0x150);Pair devices(hardware);for(int i=0;i<20;++i)devices.step();
    check(counter(devices.execution.status()["busReads"])>0,"device reads claimed snapshot source");
    auto interrupt=rom;const uint8_t irq[]={0x3e,1,0xea,0xff,0xff,0xe0,0x0f,0xfb,0,0x76};
    std::copy(std::begin(irq),std::end(irq),interrupt.begin()+0x150);interrupt[0x40]=0xd9;Pair interrupts(interrupt);
    for(int i=0;i<20;++i)interrupts.step();
    check(counter(interrupts.execution.status()["authoritativeBoundarySteps"])>0,"no interrupt/stall boundaries");
    auto dma=rom;dma[0x150]=0x3e;dma[0x151]=0xc0;dma[0x152]=0xe0;dma[0x153]=0x46;Pair transfer(dma);for(int i=0;i<12;++i)transfer.step();
    auto interior=rom;const uint8_t chain[]={0x3e,1,0x06,2,0xc3,0x60,1};std::copy(std::begin(chain),std::end(chain),interior.begin()+0x150);
    interior[0x160]=0xc3;interior[0x161]=0x52;interior[0x162]=1;Pair split(interior);
    for(int i=0;i<4;++i)split.step();
    auto beforeSplit=split.execution.state()["blocks"].size();split.step();split.step();
    check(split.execution.state()["blocks"].size()>beforeSplit+1,"interior entry did not split execution snapshots");
    auto rewritten=rom;const uint8_t ramProgram[]={0x3e,0,0xea,0,0xc0,0x3e,0xc3,0xea,1,0xc0,0x3e,0x67,0xea,2,0xc0,0x3e,1,0xea,3,0xc0,0xc3,0,0xc0,0x3e,0x3e,0xea,0,0xc0,0xc3,0,0xc0};
    std::copy(std::begin(ramProgram),std::end(ramProgram),rewritten.begin()+0x150);Pair revisions(rewritten);for(int i=0;i<18;++i)revisions.step();
    unsigned versions=0;auto revisionState=revisions.execution.state();for(auto& block:revisionState["blocks"])for(auto& instruction:block["instructions"])if(instruction["location"]==decimal(location(3,0,0xc000)))++versions;
    check(versions==2,"rewritten RAM code shared execution identity");
    // Safe preflight rejection leaves every guest effect untouched; explicit baseline can continue.
    Pair blocked(rom);blocked.snapshot.executionMemory().file.findRegister("PC")->reg->value=0x8000;
    auto fingerprint=blocked.snapshot.deterministicStateFingerprint();rejects([&]{blocked.snapshot.step();});
    check(blocked.snapshot.deterministicStateFingerprint()==fingerprint,"preflight rejection changed guest state");
    check(blocked.execution.status()["activeMode"]=="paused","unsupported backing silently fell back");blocked.execution.mode("baseline");blocked.snapshot.step();
    Pair exhausted(rom);exhausted.snapshotTrace.budget->maximum=exhausted.snapshotTrace.budget->used.load();
    fingerprint=exhausted.snapshot.deterministicStateFingerprint();rejects([&]{exhausted.snapshot.step();});check(exhausted.snapshot.deterministicStateFingerprint()==fingerprint,"budget rejection changed guest state");
    // Loss during an instruction is reported at its completed boundary, never mid-effect.
    Pair lost(rom);Record filler;for(size_t i=0;i<Capture::capacity-1;++i)lost.snapshotTrace.push(filler);
    lost.baseline.step();lost.snapshot.step();lost.execution.evidenceLost();
    check(lost.baseline.deterministicStateFingerprint()==lost.snapshot.deterministicStateFingerprint(),"evidence loss interrupted instruction effects");
    fingerprint=lost.snapshot.deterministicStateFingerprint();rejects([&]{lost.snapshot.step();});
    check(lost.snapshot.deterministicStateFingerprint()==fingerprint,"evidence-loss pause changed state");
    lost.execution.mode("baseline");lost.snapshot.step();
    auto stoppedRom=rom;stoppedRom[0x150]=0x10;stoppedRom[0x151]=0;Pair stopped(stoppedRom);
    for(int i=0;i<6;++i)stopped.step();
    // Paired restore retains analysis, replaces execution suppliers, and rejects malformed metadata atomically.
    auto dir=std::filesystem::temp_directory_path()/("space-execution-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);
    GB::GameBoyMachine machine;machine.loadRom(rom);Session session(machine,rom);session.executionMode("snapshot");
    for(int i=0;i<24;++i)session.step();
    session.input(1);session.checkpoint(dir/"checkpoint");
    auto savedFingerprint=machine.deterministicStateFingerprint();auto saved=Project::read(dir/"checkpoint/execution.json");
    for(int i=0;i<8;++i)session.step();
    auto discoveries=session.document()["dependencies"].size();session.restore(dir/"checkpoint");
    check(machine.deterministicStateFingerprint()==savedFingerprint,"checkpoint did not restore guest state");
    check(session.document()["dependencies"].size()>=discoveries,"checkpoint discarded discoveries");
    check(session.executionStatus()["epoch"]!=saved["epoch"],"restore did not branch execution epoch");
    session.checkpoint(dir/"restored");auto restored=Project::read(dir/"restored/execution.json");check(restored["values"]==saved["values"],"abandoned suppliers survived restore");
    auto malformed=saved;malformed["values"]["49152"]["block"]="999999";Project::write(dir/"checkpoint/execution.json",malformed);
    auto manifest=Project::read(dir/"checkpoint/manifest.json");auto text=malformed.dump();manifest["executionSha256"]=digest(std::span(reinterpret_cast<const uint8_t*>(text.data()),text.size()));Project::write(dir/"checkpoint/manifest.json",manifest);
    rejects([&]{session.restore(dir/"checkpoint");});check(machine.deterministicStateFingerprint()==savedFingerprint,"malformed restore partially mutated guest state");
    check(session.executionStatus()["epoch"]==restored["epoch"],"malformed restore partially mutated execution state");
    manifest=Project::read(dir/"restored/manifest.json");manifest["schemaVersion"]=1;manifest.erase("executionSha256");Project::write(dir/"restored/manifest.json",manifest);
    rejects([&]{session.restore(dir/"restored");});session.executionMode("baseline");session.restore(dir/"restored");
    // Imports are evidence only; switching mode starts fresh runtime supplier history.
    session.executionMode("snapshot");session.checkpoint(dir/"fresh");check(Project::read(dir/"fresh/execution.json")["values"].empty(),"mode activation resumed historic suppliers");
    std::filesystem::remove_all(dir);
    std::cout<<"snapshot execution parity, effects, history and checkpoints passed ("<<supported<<" base + 256 CB opcodes)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
