#ifdef NDEBUG
#undef NDEBUG
#endif
#include "machine/plugins/input/netplay/LockstepService.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/Project.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
using namespace BMMQ;
using namespace BMMQ::Netplay;
namespace {
Binding binding(Core core,const std::vector<std::uint8_t>& rom) {
    Digest session{},configuration{};session[0]=1;configuration[0]=2;
    return {.core=core,.session=session,.rom=digestFromHex(Space::digest(rom)),.configuration=configuration,.generation=1,.ownership={0x0f,0xf0}};
}
void engineCases() {
    const std::vector<std::uint8_t> rom(32768);
    auto b=binding(Core::GameGear,rom);Digest before{};before[0]=4;
    const Packet p{.binding=b,.frame=0,.before=before,.input=1,.peer=0};
    auto other=p;other.peer=1;other.input=0x10;
    LockstepEngine engine(b);
    assert(!engine.ready(before) && !engine.completeFrame());assert(engine.accept(p));assert(!engine.ready(before));
    assert(engine.accept(p) && engine.duplicates()==1);
    assert(engine.accept(other) && engine.ready(before)==0x11);assert(engine.completeFrame());assert(!engine.ready(before));
    assert(engine.accept(p));auto conflict=p;conflict.input=2;
    assert(!engine.accept(conflict) && engine.fault()==Fault::ConflictingInput);
    // The far edge of the future window must not erase committed duplicate
    // history, even when both frame numbers share a ring index.
    {LockstepEngine separate(b);assert(separate.accept(p) && separate.accept(other) && separate.ready(before) && separate.completeFrame());
     auto future=p;future.frame=128;assert(separate.accept(future));
     auto changed=p;changed.input=2;assert(!separate.accept(changed) && separate.fault()==Fault::ConflictingInput);}
    auto wire=encode(p);assert(decode(wire)==p);
    for(std::size_t i=0;i<wire.size();++i) {auto corrupt=wire;corrupt[i]^=1;assert(!decode(corrupt));}
    assert(!decode(std::span(wire.data(),wire.size()-1)));
    for(unsigned kind=0;kind<4;++kind) {
        LockstepEngine rejected(b);auto bad=p;
        if(kind==0) bad.binding.core=Core::GameBoy;
        if(kind==1) ++bad.binding.generation;
        if(kind==2) bad.binding.rom[0]^=1;
        if(kind==3) bad.binding.configuration[0]^=1;
        assert(!rejected.accept(bad) && rejected.fault()==Fault::BindingMismatch);
    }
    {LockstepEngine failed(b);auto far=p;far.frame=128;assert(!failed.accept(far) && failed.fault()==Fault::WindowExhausted);}
    {LockstepEngine failed(b);auto bad=other;bad.before[0]^=1;assert(failed.accept(p) && failed.accept(bad));assert(!failed.ready(before) && failed.fault()==Fault::FingerprintMismatch);}
    {LockstepEngine reordered(b);auto future=p;future.frame=1;auto futureOther=other;futureOther.frame=1;
     assert(reordered.accept(future) && reordered.accept(futureOther));assert(!reordered.ready(before));
     assert(reordered.accept(other) && reordered.accept(p));assert(reordered.ready(before) && reordered.completeFrame());
     assert(reordered.ready(before) && reordered.completeFrame());}
    {LockstepEngine history(b);
     for(unsigned n=0;n<130;++n) {auto a=p,c=other;a.frame=c.frame=n;assert(history.accept(a) && history.accept(c) && history.ready(before) && history.completeFrame());}
     assert(history.accept(p) && history.stale()==1);}
}
std::unique_ptr<Machine> create(Core core,const std::vector<std::uint8_t>& rom) {
    std::unique_ptr<Machine> result;
    if(core==Core::GameBoy) result=std::make_unique<GB::GameBoyMachine>();else result=std::make_unique<GameGearMachine>();
    result->loadRom(rom);return result;
}
void machineCases(Core core) {
    std::vector<std::uint8_t> rom(32768);
    const std::uint8_t gb[]{0x3e,0x10,0xe0,0x00,0xf0,0x00,0xea,0x00,0xc0,0xc3,0x04,0x01};
    const std::uint8_t gg[]{0xdb,0xdc,0x32,0x00,0xc0,0xc3,0x00,0x00};
    if(core==Core::GameBoy) std::copy(std::begin(gb),std::end(gb),rom.begin()+0x100);
    else std::copy(std::begin(gg),std::end(gg),rom.begin());
    auto a=create(core,rom),c=create(core,rom),reference=create(core,rom);
    auto ref=Space::makeCoreAdapter(*reference);auto b=binding(core,rom);
    LockstepService first(*a,b,0),second(*c,b,1);
    const auto initial=first.before();assert(first.run().state==State::Waiting && first.before()==initial);
    bool rejected=false;try {a->step();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    for(unsigned frame=0;frame<2;++frame) {
        const InputButtonMask one=frame?2:1,two=frame?0x20:0x10;
        const auto pa=first.localPacket(one),pc=second.localPacket(two);
        assert(first.submitLocal(one));assert(first.run().state==State::Waiting);
        assert(second.submitLocal(two));assert(first.acceptRemote(pc) && second.acceptRemote(pa));
        std::uint64_t instructions{},cycles{};Progress done;
        do {done=first.run(256);instructions+=done.instructions;cycles+=done.cycles;assert(done.state!=State::Faulted);}while(done.state!=State::FrameComplete);
        Progress compared;std::uint64_t otherInstructions{},otherCycles{};
        do {compared=second.run(173);otherInstructions+=compared.instructions;otherCycles+=compared.cycles;assert(compared.state!=State::Faulted);}while(compared.state!=State::FrameComplete);
        assert(instructions==otherInstructions && cycles==otherCycles && done.after==compared.after && done.carry==compared.carry);
        ref->input(one|two);
        for(std::uint64_t n=0;n<instructions;) n+=reference->runSlice({.maxInstructions=1}).progress.retiredInstructions;
        assert(stateDigest(ref->fingerprint())==done.after);
        assert(first.run().state==State::Waiting);
    }
    first.disconnect();assert(first.run().fault==Fault::Disconnected);
    // Resetting the actual core invalidates queued/acknowledged session state.
    c->loadRom(rom);assert(second.run().fault==Fault::GenerationChanged);
    // An incompatible binding is rejected before attaching an execution owner.
    auto different=create(core,rom);auto wrong=b;wrong.rom[0]^=1;
    rejected=false;try {LockstepService bad(*different,wrong,0);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    assert(different->runSlice({.maxInstructions=1}).progress.retiredInstructions<=1);
    if(core==Core::GameGear) {
        std::vector<std::uint8_t> longRom(65536);longRom[0]=0xc3;longRom[1]=0;longRom[2]=0x10;
        std::fill_n(longRom.begin()+0x1000,31000,0xdd);longRom[0x1000+31000]=0;
        auto longMachine=create(core,longRom);auto longBinding=binding(core,longRom);
        LockstepService longFrame(*longMachine,longBinding,0);
        auto remote=longFrame.localPacket(0);remote.peer=1;
        assert(longFrame.submitLocal(0) && longFrame.acceptRemote(remote));
        assert(longFrame.run().fault==Fault::FrameOverrun);
    }
    // A paused RAM change invalidates the acknowledged pre-frame fingerprint.
    auto edited=create(core,rom);LockstepService edit(*edited,b,0);
    auto local=edit.localPacket(1),remote=local;remote.peer=1;remote.input=0x10;
    assert(edit.submitLocal(1) && edit.acceptRemote(remote));
    edited->runtimeContext().write8(0xc002,42);assert(edit.run().fault==Fault::FingerprintMismatch);
}
}
int main() {engineCases();machineCases(Core::GameBoy);machineCases(Core::GameGear);
    std::cout<<"lockstep packet binding, replay/reorder bounds and exact both-core instruction/cycle/fingerprint comparison passed\n";}
