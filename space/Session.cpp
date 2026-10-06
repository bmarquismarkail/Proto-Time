#include "Session.hpp"
#include <chrono>
#include <fstream>
#include <future>
namespace BMMQ::Space {
Session::Session(GB::GameBoyMachine& machine,std::span<const uint8_t> rom,const std::filesystem::path& path,std::shared_ptr<StateBudget> budget)
 :machine_(machine),rom_(rom.begin(),rom.end()),capture_(std::make_unique<Capture>()),project_(digest(rom)) {
    if(!path.empty()&&std::filesystem::exists(path))project_=Project::load(path,digest(rom));
    if(budget)capture_->budget=std::move(budget);
    project_.startHistory();
    project_.setBudget(capture_->budget);
    execution_=std::make_unique<Execution>(machine_,*capture_);
    machine_.setAnalysisCapture(capture_.get());
    machine_.setSnapshotExecution(execution_.get());
    try { worker_=std::thread([this]{consume();}); } catch(...) {machine_.setSnapshotExecution(nullptr);machine_.setAnalysisCapture(nullptr);throw;}
}
Session::~Session(){machine_.setSnapshotExecution(nullptr);machine_.setAnalysisCapture(nullptr);stopping_.store(true);if(worker_.joinable())worker_.join();if(pinnedCharge_)capture_->budget->release(pinnedCharge_);}
void Session::consume(){
    try {
        for(;;){
            {std::lock_guard lock(mutex_);Record r;while(capture_->pop(r))project_.ingest(r);
                if(project_.exhausted())capture_->stop();}
            if(stopping_.load()&&capture_->pending()==0)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(capture_->lost()){std::lock_guard lock(mutex_);project_.gap("capture queue overflow; capture stopped");}
    } catch(const std::exception& e){stopping_.store(true);capture_->stop();std::lock_guard lock(mutex_);project_.gap(std::string("analysis failure: ")+e.what());}
}
void Session::flush(){
    // Producer is paused. Consumer holding the mutex completes the popped packet.
    while(capture_->pending()&&!stopping_.load())std::this_thread::yield();
    std::lock_guard lock(mutex_);
    if(capture_->lost()){project_.gap("capture queue overflow; capture stopped");capture_->stop();}
}
void Session::executionMode(const std::string& mode){flush();execution_->mode(mode);}
Json Session::executionStatus()const{return execution_->status();}
void Session::step(){machine_.step();flush();execution_->evidenceLost();}
void Session::save(const std::filesystem::path& path){flush();std::lock_guard lock(mutex_);project_.save(path);}
Json Session::document(){flush();std::lock_guard lock(mutex_);return project_.document();}
void Session::input(uint8_t mask){
    flush();inputMask_=mask;++inputPosition_;
    machine_.inputService().publishDigitalSnapshot(mask,machine_.inputService().currentGeneration());
    machine_.setJoypadState(mask);
    flush();
}
void Session::checkpoint(const std::filesystem::path& path){
    flush();if(std::filesystem::exists(path))throw std::invalid_argument("checkpoint already exists");
    auto tmp=path;tmp+=".tmp";if(!std::filesystem::create_directory(tmp))throw std::invalid_argument("checkpoint temporary directory exists");
    try {
        machine_.save_state(tmp/"machine.ptstate");
        std::lock_guard lock(mutex_);
        auto doc=project_.document();auto docText=doc.dump();Project::write(tmp/"project.json",doc);
        std::ifstream f(tmp/"machine.ptstate",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        auto executionState=execution_->state();executionState["romSha256"]=digest(rom_);auto executionText=executionState.dump();
        Project::write(tmp/"execution.json",executionState);
        // Validate the complete saved pair before publishing its directory.
        Project::validate(doc);
        GB::GameBoyMachine candidate;candidate.loadRom(rom_);candidate.load_state(tmp/"machine.ptstate");
        Capture candidateCapture;Execution candidateExecution(candidate,candidateCapture);
        candidateExecution.restore(executionState,false);
        Project::write(tmp/"manifest.json",{{"schemaVersion",2},{"executionSha256",digest(std::span(reinterpret_cast<const uint8_t*>(executionText.data()),executionText.size()))},{"romSha256",digest(rom_)},{"machineSha256",digest(bytes)},
            {"projectSha256",digest(std::span(reinterpret_cast<const uint8_t*>(docText.data()),docText.size()))},
            {"inputMask",machine_.currentDigitalInputMask().value_or(inputMask_)},{"inputPosition",doc.at("history").at("inputPosition")}});
        std::filesystem::rename(tmp,path);
    }catch(...){std::filesystem::remove_all(tmp);throw;}
}
void Session::restore(const std::filesystem::path& path){
    flush();auto manifest=Project::read(path/"manifest.json");
    if((manifest.at("schemaVersion")!=1&&manifest.at("schemaVersion")!=2)||manifest.at("romSha256")!=digest(rom_))throw std::invalid_argument("checkpoint ROM/schema mismatch");
    auto saved=Project::read(path/"project.json");Project::validate(saved);
    auto text=saved.dump();if(manifest.at("projectSha256")!=digest(std::span(reinterpret_cast<const uint8_t*>(text.data()),text.size())))throw std::invalid_argument("checkpoint project checksum mismatch");
    if(std::filesystem::file_size(path/"machine.ptstate")>128u*1024u*1024u)throw std::invalid_argument("checkpoint state too large");
    std::ifstream f(path/"machine.ptstate",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    if(manifest.at("machineSha256")!=digest(bytes))throw std::invalid_argument("checkpoint machine checksum mismatch");
    auto mask=manifest.at("inputMask").get<unsigned>();if(mask>255)throw std::invalid_argument("invalid checkpoint input");
    auto position=counter(manifest.at("inputPosition"));
    // Validate full machine import on a disposable machine before touching this session.
    GB::GameBoyMachine candidate;candidate.loadRom(rom_);candidate.load_state(path/"machine.ptstate");
    Json executionState;
    Capture candidateCapture;Execution candidateExecution(candidate,candidateCapture);
    if(manifest.at("schemaVersion")==1){if(execution_->status().at("requestedMode")!="baseline")throw std::invalid_argument("legacy checkpoint requires baseline execution");executionState=candidateExecution.state();}
    else {executionState=Project::read(path/"execution.json");auto executionText=executionState.dump();
        if(executionState.at("romSha256")!=digest(rom_)||manifest.at("executionSha256")!=digest(std::span(reinterpret_cast<const uint8_t*>(executionText.data()),executionText.size())))throw std::invalid_argument("checkpoint execution checksum/ROM mismatch");
        if(executionState.at("requestedMode")!=execution_->status().at("requestedMode"))throw std::invalid_argument("checkpoint execution mode mismatch");
        candidateExecution.restore(executionState,false);}
    std::lock_guard lock(mutex_);auto next=project_;next.setBudget(nullptr);next.merge(saved);next.restoreHistory(saved.at("history"));
    const auto growth=next.chargedBytes()>project_.chargedBytes()?next.chargedBytes()-project_.chargedBytes():0;
    const auto analysisCharge=capture_->budget->used.load()-execution_->chargedBytes();
    if(growth>capture_->budget->maximum-analysisCharge||candidateCapture.budget->used.load()>capture_->budget->maximum-analysisCharge-growth)throw std::invalid_argument("checkpoint combined analysis budget exceeded");
    auto stagedExecution=std::make_unique<Execution>(machine_,*capture_,std::make_shared<StateBudget>());
    stagedExecution->restore(executionState,true,false);
    if(!capture_->budget->reserve(growth))throw std::invalid_argument("analysis restore exceeds available budget");
    machine_.setSnapshotExecution(nullptr);
    machine_.setAnalysisCapture(nullptr);
    try { machine_.load_state(path/"machine.ptstate"); } catch(...) { capture_->budget->release(growth);machine_.setAnalysisCapture(capture_.get());machine_.setSnapshotExecution(execution_.get());throw; }
    machine_.setAnalysisCapture(capture_.get()); // staged validation in GameBoyMachine
    execution_.reset();stagedExecution->adoptBudget(capture_->budget);execution_=std::move(stagedExecution);machine_.setSnapshotExecution(execution_.get());
    next.setBudget(capture_->budget,false);project_=std::move(next);inputMask_=static_cast<uint8_t>(mask);inputPosition_=position;
}Json Session::analyze(){
    auto frozen=document();auto available=capture_->budget->maximum-capture_->budget->used.load()+pinnedCharge_;
    if(frozen.dump().size()*2+256>available)throw std::invalid_argument("analysis storage exhausted; previous pinned revision retained");
    // Explicit replacement releases the old frozen project before building the new one.
    // The paused control plane has no concurrent query readers.
    pinned_=nullptr;if(pinnedCharge_)capture_->budget->release(pinnedCharge_);pinnedCharge_=0;
    if(!capture_->budget->reserve(available))throw std::invalid_argument("analysis reservation unavailable");
    Json next;
    try{auto task=std::async(std::launch::async,[capture=std::move(frozen),available]()mutable{return analyzeProject(std::move(capture),available);});next=task.get();}
    catch(...){capture_->budget->release(available);throw;}
    auto charge=next.dump().size()*2+256;
    if(charge>available){capture_->budget->release(available);throw std::invalid_argument("pinned analysis exceeds storage budget; no new analysis published");}
    next["analysis"]["retainedBytes"]=decimal(charge);capture_->budget->release(available-charge);
    pinnedCharge_=charge;pinned_=std::move(next);
    return {{"id",pinned_["analysis"]["id"]},{"sourceRevision",pinned_["analysis"]["sourceRevision"]},{"incomplete",pinned_["analysis"]["incomplete"]},
        {"limitations",pinned_["analysis"]["limitations"]},{"routines",pinned_["analysis"]["routines"].size()},{"findings",pinned_["analysis"]["findings"].size()},{"loops",pinned_["analysis"]["loops"].size()}};
}
Json Session::query(const Json& request)const {if(pinned_.is_null())throw std::invalid_argument("run analyze to pin a capture first");return queryAnalysis(pinned_,request);}
void Session::saveAnalysis(const std::filesystem::path& path)const {if(pinned_.is_null())throw std::invalid_argument("no pinned analysis");Project::write(path,pinned_);}
void Session::annotate(const std::string& instruction,const Json& annotation){
    if(pinned_.is_null())throw std::invalid_argument("run analyze before annotation");
    auto next=pinned_;auto note=annotation;note["analysisId"]=pinned_["analysis"]["id"];annotateDocument(next,instruction,note);
    flush();std::lock_guard lock(mutex_);auto charge=note.dump().size()+512;
    if(!capture_->budget->reserve(charge))throw std::invalid_argument("pinned annotation exceeds budget");
    try{project_.annotate(instruction,note);}catch(...){capture_->budget->release(charge);throw;}
    pinnedCharge_+=charge;next["analysis"]["retainedBytes"]=decimal(pinnedCharge_);pinned_=std::move(next);
}
Json Session::runUntil(const Json& command){
    const bool target=command.contains("instruction"),event=command.contains("event");if(target==event)throw std::invalid_argument("run_until requires exactly one instruction or hardware event");
    auto bound=command.value("count",Json(10000));if(!bound.is_number_integer()||bound<1||bound>1000000)throw std::invalid_argument("run_until count must be 1–1000000");
    uint64_t limit=bound.get<uint64_t>(),count=0;std::string reason;
    Json instruction,observed;
    if(target){if(pinned_.is_null())throw std::invalid_argument("instruction targets require pinned analysis");auto id=command.at("instruction").get<std::string>();
        if(!pinned_["instructions"].contains(id))throw std::invalid_argument("unknown target instruction");
        instruction=pinned_["instructions"][id];auto space=counter(instruction["location"])>>32;
        if(space==0||space>=5)throw std::invalid_argument("instruction target has unresolved or device backing; matching would reread hardware");}
    auto& map=dynamic_cast<GB::GameBoyMemoryMap&>(machine_.executionMemory().backingStore());
    auto& watch=capture_->watch;watch={};
    if(event){auto& spec=command.at("event");auto access=spec.at("access").get<std::string>();if(access!="read"&&access!="write")throw std::invalid_argument("event access must be read or write");watch.write=access=="write";
        if(spec.contains("address")==spec.contains("category"))throw std::invalid_argument("event requires address or category");
        if(spec.contains("address")){auto addr=spec["address"];if(!addr.is_number_integer()||addr<0||addr>65535)throw std::invalid_argument("invalid event address");watch.first=watch.last=addr.get<uint16_t>();
            if(hardwareDescriptor(map.analysisLocation(watch.first),watch.write).is_null())throw std::invalid_argument("event address is not hardware");}
        else {auto category=spec["category"].get<std::string>();bool found=false;
            for(unsigned addr=0;addr<65536;++addr){auto h=hardwareDescriptor(map.analysisLocation(addr),watch.write);if(!h.is_null()&&h["category"]==category){if(!found)watch.first=addr;watch.last=addr;found=true;}}
            if(!found)throw std::invalid_argument("unknown hardware category");
            watch.categoryMask=hardwareCategoryBit(map.analysisLocation(watch.first),watch.write);}
        watch.enabled=true;
    }
    struct Guard{Capture::Watch& watch;~Guard(){watch.enabled=false;}}guard{watch};
    while(true){
        if(capture_->stopped()){reason="evidence_loss";break;}
        if(execution_->status()["activeMode"]=="paused"){reason="execution_pause";break;}
        if(target){auto pc=machine_.executionMemory().file.findRegister("PC")->reg->value;
            if(pc==instruction["address"]&&decimal(map.analysisLocation(pc))==instruction["location"].get<std::string>()&&!machine_.snapshotBoundaryOnly()){
                for(unsigned i=0;i<instruction["length"].get<unsigned>();++i){auto space=map.analysisLocation(uint16_t(pc+i))>>32;
                    if(space==0||space>=5)throw std::invalid_argument("instruction target operands cross hardware or unresolved backing");}
                std::string actual;constexpr char hex[]="0123456789abcdef";auto phase=capture_->phase;capture_->phase=Kind::Inspection;
                for(unsigned i=0;i<instruction["length"].get<unsigned>();++i){uint8_t value=0;machine_.executionMemory().read(std::span(&value,1),uint16_t(pc+i));actual+=hex[value>>4];actual+=hex[value&15];}capture_->phase=phase;
                if(capture_->stopped()){reason="evidence_loss";break;}
                if(actual==instruction["bytes"].get<std::string>()){reason="target_reached";break;}
            }
        }
        if(count>=limit){reason="step_limit";break;}
        try{step();++count;}catch(const ExecutionPaused&){reason="execution_pause";break;}
        if(capture_->stopped()){reason="evidence_loss";break;}
        if(execution_->status()["activeMode"]=="paused"){reason="execution_pause";break;}
        if(event&&watch.matched){reason="event_observed";auto& r=watch.record;observed={{"address",r.address},{"value",r.value},{"location",decimal(r.location)},{"access",r.isWrite?"write":"read"},{"origin","cpu"}};break;}
    }
    if(reason.empty())reason="step_limit";
    return {{"reason",reason},{"count",decimal(count)},{"event",observed},{"execution",execution_->status()},{"fingerprint",machine_.deterministicStateFingerprint()}};
}

}
