#include "LockstepService.hpp"
#include "inst_cycle/executor/ExecutorPolicyRegistry.hpp"
#include "machine/modding/NativeMod.hpp"
#include <limits>
#include <stdexcept>

namespace BMMQ::Netplay {
LockstepService::LockstepService(Machine& machine,Binding binding,std::uint8_t localPeer,RemoteTransport* transport):
    machine_(machine),adapter_(Space::makeCoreAdapter(machine)),control_(dynamic_cast<Debug::IDebugMachineV1&>(machine)),
    engine_(binding),transport_(transport),machineGeneration_(machine.observationGeneration()),localPeer_(localPeer) {
    const auto core=adapter_->model().id=="gameboy"?Core::GameBoy:Core::GameGear;
    if(localPeer>1 || binding.core!=core || binding.rom!=digestFromHex(adapter_->romDigest()) || !compatible())
        throw std::invalid_argument("netplay requires an exact ROM binding and exclusive baseline execution without host input, plugins or native state");
    before_=stateDigest(adapter_->fingerprint());
    // Reuse the existing instruction-control lease; recording is disabled and
    // no debugger transport, rules, hooks or mutable remote machine view exists.
    control_.connectDebugEngine(&lease_);connected_=true;
}
LockstepService::~LockstepService() { if(connected_) control_.connectDebugEngine(nullptr); }
void LockstepService::lane()const {
    if(owner_!=std::this_thread::get_id()) throw std::logic_error("netplay machine lane changed");
}
bool LockstepService::compatible()const {
    return machine_.attachedExecutorPolicy().backend()==ExecutionBackend::Baseline &&
        machine_.pluginManager().size()==0 && machine_.inputService().state()==InputLifecycleState::Detached &&
        !machine_.modHost().hasRegions() && !Modding::NativeMod::hasCohort(machine_.modHost());
}
void LockstepService::pump() {
    if(machine_.observationGeneration()!=machineGeneration_) engine_.fail(Fault::GenerationChanged);
    if(!compatible()) engine_.fail(Fault::IncompatibleMachine);
    if(transport_) {
        const auto failure=transport_->fault();if(failure!=Fault::None) engine_.fail(failure);
        for(unsigned n=0;n<64;++n) {
            const auto packet=transport_->receive();if(!packet) break;
            (void)acceptRemote(*packet);
        }
    }
}
Packet LockstepService::localPacket(InputButtonMask mask) {
    lane();pump();
    if(engine_.fault()!=Fault::None || started_ || (mask&~engine_.binding().ownership[localPeer_]))
        throw std::invalid_argument("local frame input unavailable or outside peer ownership");
    return {.binding=engine_.binding(),.frame=engine_.frame(),.before=before_,.input=mask,.peer=localPeer_};
}
bool LockstepService::submitLocal(InputButtonMask mask) {
    const auto packet=localPacket(mask);
    if(!engine_.accept(packet)) return false;
    local_=packet;
    if(transport_ && !transport_->send(packet)) { engine_.fail(transport_->fault());return false; }
    return true;
}
bool LockstepService::acceptRemote(const Packet& packet) {
    lane();
    if(packet.peer==localPeer_) {engine_.fail(Fault::InvalidPacket);return false;}
    return engine_.accept(packet);
}
Progress LockstepService::run(std::uint64_t budget) {
    lane();pump();
    Progress progress{.state=State::Waiting,.frame=engine_.frame(),.carry=carry_,.before=before_};
    auto fault=[&] {
        lease_.cancel();progress.state=State::Faulted;progress.fault=engine_.fault();return progress;
    };
    if(engine_.fault()!=Fault::None) return fault();
    if(!budget || budget>10'000'000) throw std::invalid_argument("netplay instruction budget must be 1..10000000");
    if(!started_) {
        // Recheck state before publication: paused edits or lifecycle changes
        // cannot silently consume already acknowledged frame input.
        const auto current=stateDigest(adapter_->fingerprint());
        if(current!=before_) {engine_.fail(Fault::FingerprintMismatch);return fault();}
        const auto mask=engine_.ready(before_);
        if(engine_.fault()!=Fault::None) return fault();
        if(!mask) return progress;
        adapter_->input(*mask);started_=true;
    }
    progress.state=State::Running;
    for(std::uint64_t n=0;n<budget;++n) {
        pump();if(engine_.fault()!=Fault::None) return fault();
        lease_.begin();
        ExecutionSliceResult step;
        try {step=machine_.runSlice({.maxInstructions=1});}
        catch(...) {lease_.cancel();engine_.fail(Fault::ExecutionFailure);return fault();}
        lease_.cancel();
        if(!step.progress.retiredInstructions) {
            // A machine lifecycle boundary may retire no instruction (GB boot
            // entry). Return an explicit pause; the next call resumes the same
            // acknowledged frame and never republishes its input.
            progress.state=State::BudgetPaused;return progress;
        }
        progress.instructions+=step.progress.retiredInstructions;progress.cycles+=step.progress.retiredCycles;
        if(!step.progress.retiredCycles || step.progress.retiredCycles>std::numeric_limits<std::uint64_t>::max()-carry_) {
            engine_.fail(Fault::ExecutionFailure);return fault();
        }
        carry_+=step.progress.retiredCycles;progress.carry=carry_;
        const auto period=cyclesPerFrame(engine_.binding().core);
        if(carry_>=period) {
            if(carry_-period>=period) {engine_.fail(Fault::FrameOverrun);return fault();}
            carry_-=period;progress.carry=carry_;
            progress.after=stateDigest(adapter_->fingerprint());
            if(!engine_.completeFrame()) {engine_.fail(Fault::ExecutionFailure);return fault();}
            started_=false;local_.reset();before_=progress.after;
            progress.state=State::FrameComplete;return progress;
        }
    }
    progress.state=State::BudgetPaused;return progress;
}
bool LockstepService::acknowledged() {
    lane();pump();
    if(started_) return false;
    if(stateDigest(adapter_->fingerprint())!=before_) {engine_.fail(Fault::FingerprintMismatch);return false;}
    return engine_.ready(before_).has_value();
}
void LockstepService::disconnect() {lane();engine_.fail(Fault::Disconnected);lease_.cancel();}
}
