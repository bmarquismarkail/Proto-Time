#include "Session.hpp"
#include <chrono>
#include <fstream>
namespace BMMQ::Space {
Session::Session(GB::GameBoyMachine& machine,std::span<const uint8_t> rom,const std::filesystem::path& path)
 :machine_(machine),rom_(rom.begin(),rom.end()),capture_(std::make_unique<Capture>()),project_(digest(rom)) {
    if(!path.empty()&&std::filesystem::exists(path))project_=Project::load(path,digest(rom));
    project_.startHistory();
    machine_.setAnalysisCapture(capture_.get());
    try { worker_=std::thread([this]{consume();}); } catch(...) {machine_.setAnalysisCapture(nullptr);throw;}
}
Session::~Session(){machine_.setAnalysisCapture(nullptr);stopping_.store(true);if(worker_.joinable())worker_.join();}
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
        Project::write(tmp/"manifest.json",{{"schemaVersion",1},{"romSha256",digest(rom_)},{"machineSha256",digest(bytes)},
            {"projectSha256",digest(std::span(reinterpret_cast<const uint8_t*>(docText.data()),docText.size()))},
            {"inputMask",machine_.currentDigitalInputMask().value_or(inputMask_)},{"inputPosition",doc.at("history").at("inputPosition")}});
        std::filesystem::rename(tmp,path);
    }catch(...){std::filesystem::remove_all(tmp);throw;}
}
void Session::restore(const std::filesystem::path& path){
    flush();auto manifest=Project::read(path/"manifest.json");
    if(manifest.at("schemaVersion")!=1||manifest.at("romSha256")!=digest(rom_))throw std::invalid_argument("checkpoint ROM/schema mismatch");
    auto saved=Project::read(path/"project.json");Project::validate(saved);
    auto text=saved.dump();if(manifest.at("projectSha256")!=digest(std::span(reinterpret_cast<const uint8_t*>(text.data()),text.size())))throw std::invalid_argument("checkpoint project checksum mismatch");
    if(std::filesystem::file_size(path/"machine.ptstate")>128u*1024u*1024u)throw std::invalid_argument("checkpoint state too large");
    std::ifstream f(path/"machine.ptstate",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    if(manifest.at("machineSha256")!=digest(bytes))throw std::invalid_argument("checkpoint machine checksum mismatch");
    auto mask=manifest.at("inputMask").get<unsigned>();if(mask>255)throw std::invalid_argument("invalid checkpoint input");
    auto position=counter(manifest.at("inputPosition"));
    // Validate full machine import on a disposable machine before touching this session.
    GB::GameBoyMachine candidate;candidate.loadRom(rom_);candidate.load_state(path/"machine.ptstate");
    std::lock_guard lock(mutex_);auto next=project_;next.merge(saved);next.restoreHistory(saved.at("history"));
    machine_.setAnalysisCapture(nullptr);
    try { machine_.load_state(path/"machine.ptstate"); } catch(...) { machine_.setAnalysisCapture(capture_.get());throw; }
    machine_.setAnalysisCapture(capture_.get()); // staged validation in GameBoyMachine
    project_=std::move(next);inputMask_=static_cast<uint8_t>(mask);inputPosition_=position;
}
}
