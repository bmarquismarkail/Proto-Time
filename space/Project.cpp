#include "Project.hpp"
#include "Meaning.hpp"
#include "Analysis.hpp"
#include "Porting.hpp"
#include <fstream>
#include <set>
#include <chrono>
#include <charconv>
#include <algorithm>
namespace BMMQ::Space {
std::string decimal(uint64_t v){return std::to_string(v);}
uint64_t counter(const Json& j){
    if(!j.is_string())throw std::invalid_argument("counter must be a decimal string");
    const auto& s=j.get_ref<const std::string&>();uint64_t v=0;
    auto r=std::from_chars(s.data(),s.data()+s.size(),v);
    if(s.empty()||r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw std::invalid_argument("invalid counter");
    return v;
}
namespace {
std::string hashText(const std::string& text){return digest(std::span(reinterpret_cast<const uint8_t*>(text.data()),text.size()));}
std::string bytesHex(std::span<const uint8_t> bytes){constexpr char hex[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=hex[b>>4];s+=hex[b&15];}return s;}
std::string loc(uint64_t n){return decimal(n);}
Json origin(const std::string& branch,uint64_t seq,const std::string& instruction,std::string kind){return {{"branch",branch},{"sequence",decimal(seq)},{"instruction",instruction},{"kind",kind}};}
}
Project::Project(std::string hash,size_t budget,std::string core):model_(&coreModel(core)),budget_(budget){
    instance_=hashText(hash+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));branch_=instance_+":0";
    state_={{"schemaVersion",2},{"core",core},{"romSha256",hash},{"instructions",Json::object()},
      {"edges",Json::array()},{"dependencies",Json::array()},{"gaps",Json::array()},{"annotations",Json::object()},
      {"transfers",Json::array()},{"inputs",Json::array()},{"boundaries",Json::array()},{"sessions",Json::array({instance_})}};
}
void Project::gap(std::string reason){
    if(state_["gaps"].size()<1024) state_["gaps"].push_back({{"branch",branch_},{"sequence",decimal(visit_)},{"reason",reason}});
    active_=false;previous_.clear();previousTransfer_.clear();state_.erase("analysis");++revision_;
}
void Project::ingest(const Record& r){
    if(exhausted_)return;
    if(r.kind==Kind::Input){
        if(512>budget_-std::min(budget_,used_)||(sharedBudget_&&!sharedBudget_->reserve(512))){gap("analysis budget exhausted; capture stopped");exhausted_=true;return;}
        used_+=512;state_["inputs"].push_back({{"branch",branch_},{"sequence",decimal(visit_)},{"position",decimal(++inputPosition_)},{"mask",r.value}});++revision_;return;
    }
    if(r.kind==Kind::Begin){if(active_)gap("incomplete instruction capture");begin_=r;accesses_.clear();fetchedBytes_.clear();fetchedBackings_=Json::array();active_=true;return;}
    if(r.kind==Kind::End){if(active_)finish(r);active_=false;return;}
    if(!active_ && (r.kind==Kind::Device || r.kind==Kind::Dma || r.kind==Kind::Write || r.kind==Kind::Mapping)) {
        begin_=Record{};accesses_={r};Record boundary;boundary.kind=Kind::End;finish(boundary);return;
    }
    if(active_&&r.kind==Kind::Fetch&&r.fetchLength){
        if(r.fetchLength>r.fetchBytes.size()||fetchedBytes_.size()+r.fetchLength>model_->maximumInstructionLength){gap("instruction fetch limit");return;}
        if(!fetchedBackings_.empty()&&counter(fetchedBackings_.back()["location"])+fetchedBackings_.back()["length"].get<size_t>()==r.location)
            fetchedBackings_.back()["length"]=fetchedBackings_.back()["length"].get<size_t>()+r.fetchLength;
        else fetchedBackings_.push_back({{"offset",fetchedBytes_.size()},{"location",decimal(r.location)},{"length",r.fetchLength}});
        fetchedBytes_.insert(fetchedBytes_.end(),r.fetchBytes.begin(),r.fetchBytes.begin()+r.fetchLength);
    }
    if(active_){if(accesses_.size()>=2048){gap("instruction access limit; capture stopped");exhausted_=true;return;}accesses_.push_back(r);}
}
void Project::finish(const Record& end){
    // A conservative allocation charge includes node/map/string overhead, not just payload bytes.
    if(model_->id=="gamegear"&&fetchedBytes_.size()!=end.length){gap("incomplete instruction fetch evidence; capture stopped");exhausted_=true;return;}
    auto instructionBytes=model_->id=="gamegear"?std::span<const uint8_t>(fetchedBytes_):std::span(end.bytes.data(),std::min<uint32_t>(3,end.length));
    const bool fullFetch=model_->id=="gamegear"||(!fetchedBytes_.empty()&&fetchedBytes_.size()==end.length);
    size_t charge=3072+accesses_.size()*768+instructionBytes.size()*4;
    if(charge>budget_-std::min(budget_,used_)||(sharedBudget_&&!sharedBudget_->reserve(charge))){gap("analysis budget exhausted; capture stopped");exhausted_=true;return;}
    used_+=charge;++visit_;++revision_;state_.erase("analysis");
    std::string id;
    auto info=decodeCore(model_->id,instructionBytes,begin_.registers[5],end.registers[5],static_cast<uint8_t>(begin_.registers[0]));
    if(end.length){
        id=hashText(loc(begin_.location)+":"+bytesHex(instructionBytes)+(fullFetch?":"+fetchedBackings_.dump():""));
        auto& n=state_["instructions"][id];
        if(n.is_null()) n={{"id",id},{"location",loc(begin_.location)},{"address",begin_.registers[5]},
           {"bytes",bytesHex(instructionBytes)},{"length",end.length},{"text",info.text},{"flow",info.flow},
           {"conditional",info.conditional},{"captures",Json::object()},{"registers",Json::object()},{"hardware",Json::array()}};
        if(fullFetch)n["fetchBackings"]=fetchedBackings_;
        n["lastVisit"]={{"branch",branch_},{"sequence",decimal(visit_)}};
        n["lastCycles"]=decimal(end.cycles);
        if(info.conditional)n["lastOutcome"]=info.taken?"taken":"not taken";
        auto edge=[&](const std::string& from,uint64_t target,const std::string& kind){
            Json e={{"from",from},{"targetLocation",loc(target)},{"kind",kind}};
            if(std::find(state_["edges"].begin(),state_["edges"].end(),e)==state_["edges"].end())state_["edges"].push_back(e);
        };
        if(!previous_.empty()) {
            Json e={{"from",previous_},{"targetLocation",loc(begin_.location)},{"toInstruction",id},{"kind","observed"}};
            if(std::find(state_["edges"].begin(),state_["edges"].end(),e)==state_["edges"].end())state_["edges"].push_back(e);
        }
        if(info.flow=="fallthrough")edge(id,end.location,"fallthrough");
        else if(info.flow=="branch"||info.flow=="call"||info.flow=="return"||info.flow=="indirect"){
            edge(id,end.location,info.flow);
            if(info.conditional||info.flow=="call") {
                uint16_t fall=uint16_t(begin_.registers[5]+end.length);
                auto base=begin_.location&~uint64_t(65535);
                if(model_->id=="gameboy"&&fall<0x4000 && (begin_.location>>32)==1)base=location(1,0,0);
                edge(id,end.fallthroughLocation?end.fallthroughLocation:base|(fall<0x8000?uint64_t(fall&0x3fff):uint64_t(fall)),"fallthrough");
            }
            if(info.direct){auto base=begin_.location&~uint64_t(65535);if(model_->id=="gameboy"&&info.target<0x4000)base=location(1,0,0);edge(id,end.targetLocation?end.targetLocation:base|(info.target&0x3fff),"target");}
        }
        if(!previousTransfer_.empty())for(auto it=state_["transfers"].rbegin();it!=state_["transfers"].rend();++it)if((*it)["id"]==previousTransfer_){(*it)["nextInstruction"]=id;break;}
        previousTransfer_=instance_+":"+decimal(++branchCounter_);
        state_["transfers"].push_back({{"id",previousTransfer_},{"instruction",id},{"branch",branch_},{"sequence",decimal(visit_)},
            {"flow",info.flow},{"taken",!info.conditional||info.taken},{"root",previous_.empty()},
            {"targetLocation",decimal(end.location)},{"fallthroughLocation",decimal(end.fallthroughLocation)}});
        previous_=id;
    } else {previous_.clear();previousTransfer_.clear();id="";
        if(begin_.registers!=end.registers&&end.cycles){
            previousTransfer_=instance_+":"+decimal(++branchCounter_);
            state_["transfers"].push_back({{"id",previousTransfer_},{"instruction",""},{"branch",branch_},{"sequence",decimal(visit_)},
                {"flow","interrupt"},{"taken",true},{"root",false},{"targetLocation",decimal(end.location)},{"fallthroughLocation","0"}});
        }
    }
    auto dep=[&](const std::string& key,uint16_t value,std::string access,bool accepted,std::string kind){
        Json supplier=writers_.contains(key)?writers_[key]:origin(branch_,0,"",kind=="device"?"device":"initialization");
        if(values_.contains(key)&&values_[key]!=value&&access=="read")supplier=origin(branch_,visit_,"","unresolved");
        auto token=instance_+":"+decimal(++branchCounter_);
        state_["dependencies"].push_back({{"id",token},{"instruction",id},{"branch",branch_},{"sequence",decimal(visit_)},
          {"location",key},{"value",value},{"access",access},{"accepted",accepted},{"supplier",supplier},{"kind",kind},{"origin",kind=="cpu"?"cpu":kind=="boundary"?"boundary":"unknown"}});
        if(access=="write"&&accepted&&kind!="mapping"){writers_[key]=origin(branch_,visit_,id,kind);values_[key]=value;}
        if(!id.empty()){
            auto& n=state_["instructions"][id];
            // Read values refresh captures but never update writer ownership.
            if(access=="read"||accepted)n["captures"][key]={{"value",value},{"sequence",decimal(visit_)},{"branch",branch_}};
            if(access=="read"||accepted) views_[id]["captures"][key]=n["captures"][key];
            if(key.rfind("m:",0)==0){auto v=std::stoull(key.substr(2));auto space=v>>32;
                if(space==4||space==5||space==6){std::string label=space==4?"video":space==5?"device":"input";
                    if(std::find(n["hardware"].begin(),n["hardware"].end(),label)==n["hardware"].end())n["hardware"].push_back(label);}}
        }
    };
    if(end.length){for(size_t lane=0;lane<model_->lanes.size();++lane){auto key="r:"+std::string(model_->lanes[lane].name);
        if(info.reads&(1<<lane)){dep(key,model_->lane(begin_,lane),"read",true,"cpu");
            if(end.executionSource){auto& d=state_["dependencies"].back();d["executionSource"]="snapshot";d["executionSupplier"]={{"sequence",decimal(end.registerSuppliers[lane])},{"block",decimal(end.registerSupplierBlocks[lane])},{"epoch",decimal(end.registerSupplierEpochs[lane])}};}}
        if((info.writes&(1u<<lane))||model_->lane(begin_,lane)!=model_->lane(end,lane))dep(key,model_->lane(end,lane),"write",true,"cpu");
        state_["instructions"][id]["registers"][model_->lanes[lane].name]=model_->lane(end,lane);
        views_[id]["registers"][model_->lanes[lane].name]=model_->lane(end,lane);
        views_[id]["lastVisit"]={{"branch",branch_},{"sequence",decimal(visit_)}};
    }}
    for(const auto& a:accesses_){
        if(a.kind==Kind::Fetch||a.kind==Kind::Inspection)continue;
        dep("m:"+loc(a.location),a.value,a.isWrite?"write":"read",a.accepted,
            (a.origin==AccessOrigin::Device || a.kind==Kind::Device || ((a.location>>32)==5) || ((a.location>>32)==6))?"device":a.kind==Kind::Dma?"dma":a.kind==Kind::Mapping?"mapping":end.length?"cpu":"boundary");
        constexpr const char* origins[]={"unknown","cpu","device","dma","boundary","inspection"};
        state_["dependencies"].back()["origin"]=origins[static_cast<unsigned>(a.origin)];
        if((a.location>>32)==7)state_["dependencies"].back()["effectivePort"]=a.address;
        if(a.kind==Kind::Read){auto& d=state_["dependencies"].back();d["executionSource"]=a.executionSource==1?"snapshot":a.executionSource==2?"initialization":"bus";
            if(a.executionSource)d["executionSupplier"]={{"sequence",decimal(a.supplierSequence)},{"block",decimal(a.supplierBlock)},{"epoch",decimal(a.supplierEpoch)}};}
    }
    if(!end.length){ // register mutations caused by interrupt entry, not a fabricated instruction
        state_["boundaries"].push_back({{"branch",branch_},{"sequence",decimal(visit_)},{"cycles",decimal(end.cycles)},
            {"kind",end.cycles==0?"device-update":begin_.registers==end.registers?"stall":"interrupt"},{"before",std::vector<uint16_t>(begin_.registers.begin(),begin_.registers.begin()+model_->pairs.size())},{"after",std::vector<uint16_t>(end.registers.begin(),end.registers.begin()+model_->pairs.size())}});
        for(size_t lane=0;lane<model_->lanes.size();++lane)if(model_->lane(begin_,lane)!=model_->lane(end,lane))dep("r:"+std::string(model_->lanes[lane].name),model_->lane(end,lane),"write",true,"boundary");
    }
}
Json Project::history()const{return {{"branch",branch_},{"visit",decimal(visit_)},{"inputPosition",decimal(inputPosition_)},{"writers",writers_},{"values",values_},{"views",views_}};}
void Project::restoreHistory(const Json& h){
    auto w=h.at("writers"),v=h.at("values");auto visit=counter(h.at("visit"));auto views=h.at("views");if(!views.is_object())throw std::invalid_argument("invalid views");
    if(!w.is_object()||!v.is_object()||w.size()!=v.size())throw std::invalid_argument("invalid writer history");
    for(auto it=w.begin();it!=w.end();++it){if(!v.contains(it.key()))throw std::invalid_argument("missing history value");counter(it.value().at("sequence"));
        auto id=it.value().at("instruction").get<std::string>();if(!id.empty()&&!state_["instructions"].contains(id))throw std::invalid_argument("unknown supplier");}
    auto inputPosition=h.contains("inputPosition")?counter(h["inputPosition"]):0;
    writers_=std::move(w);values_=std::move(v);views_=std::move(views);inputPosition_=inputPosition;visit_=visit;state_.erase("analysis");previousTransfer_.clear();branch_=instance_+":"+decimal(++branchCounter_);previous_.clear();active_=false;++revision_;
}
Json Project::document()const{
    Json out=state_;out["revision"]=decimal(revision_);out["history"]=history();out["blocks"]=Json::array();
    std::map<std::string,std::string> at,successor,owner;std::map<std::string,size_t> incoming;
    std::set<std::string> ambiguous;
    for(auto it=state_["instructions"].begin();it!=state_["instructions"].end();++it){auto key=it.value()["location"].get<std::string>();if(at.contains(key))ambiguous.insert(key);at[key]=it.key();}
    Json edges=Json::array();
    std::map<std::string,std::shared_ptr<const BlockSnapshot>> currentSnapshots;
    for(auto e:state_["edges"]){auto target=e["targetLocation"].get<std::string>();e["to"]=e.contains("toInstruction")?e["toInstruction"]:Json(at.contains(target)&&!ambiguous.contains(target)?at.at(target):"unresolved:"+target);
        auto from=e["from"].get<std::string>(),to=e["to"].get<std::string>();
        if(e["kind"]=="fallthrough" && state_["instructions"][from]["flow"]=="fallthrough")successor[from]=to;
        edges.push_back(e);
    }
    // Count distinct predecessor nodes, not duplicate observed/static edge evidence.
    std::map<std::string,std::set<std::string>> predecessors;
    for(auto& e:edges)predecessors[e["to"].get<std::string>()].insert(e["from"].get<std::string>());
    for(auto& [k,p]:predecessors)incoming[k]=p.size();
    auto make=[&](const std::string& start){
        Json block={{"id",start},{"instructions",Json::array()},{"snapshot",Json::object()},
          {"registers",Json::object()},{"conversion","not assessed"},{"verification","not assessed"}};
        auto cur=start;std::set<std::string> local;
        while(state_["instructions"].contains(cur)&&!owner.contains(cur)&&local.insert(cur).second){
            owner[cur]=start;Json n=views_.contains(cur)?views_[cur]:Json{{"captures",Json::object()},{"registers",Json::object()},{"lastVisit",nullptr}};block["instructions"].push_back(cur);
            for(auto it=n["captures"].begin();it!=n["captures"].end();++it){auto& val=block["snapshot"][it.key()];
                if(val.is_null()||counter(val["sequence"])<=counter(it.value()["sequence"]))val=it.value();}
            block["registers"]=n["registers"];block["lastVisit"]=n["lastVisit"];block["terminal"]=cur;
            if(!successor.contains(cur))break;
            const auto& next=successor.at(cur);if(incoming[next]!=1)break;cur=next;
        }
        // Materialize exactly one sparse MemorySnapshot for this captured block.
        const auto stamp=block["snapshot"].dump()+block["registers"].dump();
        std::shared_ptr<const BlockSnapshot> shadow;
        if(blockSnapshots_.contains(start)&&blockSnapshots_.at(start)->stamp==stamp)shadow=blockSnapshots_.at(start);
        else {
            auto fresh=std::make_shared<BlockSnapshot>();fresh->stamp=stamp;
            std::map<uint64_t,uint8_t> sorted;
            for(auto it=block["snapshot"].begin();it!=block["snapshot"].end();++it)if(it.key().rfind("m:",0)==0)
                sorted[std::stoull(it.key().substr(2))]=it.value()["value"].get<uint8_t>();
            for(auto& [key,value]:sorted)fresh->memory.write(std::span(&value,1),key);
            for(auto it=block["registers"].begin();it!=block["registers"].end();++it)fresh->memory.findOrCreateNewRegister(it.key())->value=it.value().get<uint16_t>();
            shadow=std::move(fresh);
        }
        currentSnapshots[start]=shadow;
        block["rawBytes"]=shadow->memory.mem.data();block["pools"]=Json::array();
        for(auto& p:shadow->memory.mem.pools())block["pools"].push_back({decimal(p.first),decimal(p.second)});
        out["blocks"].push_back(std::move(block));
    };
    for(auto it=state_["instructions"].begin();it!=state_["instructions"].end();++it){bool continuation=false;
        for(auto& [from,to]:successor)if(to==it.key()&&incoming[to]==1){continuation=true;break;}
        if(!continuation&&!owner.contains(it.key()))make(it.key());}
    for(auto it=state_["instructions"].begin();it!=state_["instructions"].end();++it)if(!owner.contains(it.key()))make(it.key());
    out["graphEdges"]=Json::array();for(auto e:edges){e["from"]=owner.at(e["from"].get<std::string>());auto to=e["to"].get<std::string>();if(owner.contains(to))e["to"]=owner.at(to);
        if(e["from"]!=e["to"]||e["kind"]!="fallthrough")out["graphEdges"].push_back(e);}
    blockSnapshots_=std::move(currentSnapshots);
    return out;
}
Json Project::read(const std::filesystem::path& p){
    if(std::filesystem::file_size(p)>128u*1024u*1024u)throw std::invalid_argument("project exceeds 128 MiB");
    std::ifstream f(p);if(!f)throw std::runtime_error("cannot read "+p.string());
    return Json::parse(f,[](int depth, Json::parse_event_t, Json&){if(depth>64)throw std::invalid_argument("project nesting exceeds 64");return true;},true,false);
}
void Project::write(const std::filesystem::path& p,const Json& j){
    auto temp=p;temp+=".tmp-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count());std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("cannot write "+temp.string());
    f<<j.dump(2);f.close();if(!f)throw std::runtime_error("project write failed");std::filesystem::rename(temp,p);
}
void Project::validate(const Json& j){
    if((j.at("schemaVersion")!=1&&j.at("schemaVersion")!=2)||(j.at("schemaVersion")==1&&j.at("core")!="gameboy"))throw std::invalid_argument("unsupported project schema/core");
    const auto& model=coreModel(j.at("core").get<std::string>());
    auto hash=j.at("romSha256").get<std::string>();if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("invalid ROM digest");
    if(!j.at("instructions").is_object()||!j.at("edges").is_array()||!j.at("dependencies").is_array()||!j.at("annotations").is_object()||!j.at("gaps").is_array())throw std::invalid_argument("invalid project collections");
    counter(j.at("revision"));
    if(!j.at("inputs").is_array()||!j.at("sessions").is_array()||!j.at("history").is_object())throw std::invalid_argument("invalid project history collections");
    auto checkKey=[&](const std::string& key) {
        if(key.rfind("m:",0)==0){auto address=counter(Json(key.substr(2)));if((address>>32)>(model.id=="gamegear"?11:6))throw std::invalid_argument("invalid address space");}
        else if(key.rfind("r:",0)==0){bool found=false;for(auto lane:model.lanes)found=found||key=="r:"+std::string(lane.name);if(!found)throw std::invalid_argument("invalid register");}
        else throw std::invalid_argument("invalid state location");
    };
    auto bounded=[](const Json& value,unsigned maximum) {
        if(!value.is_number_integer()||value<0||value>maximum)throw std::invalid_argument("invalid numeric range");
    };
    auto checkCaptures=[&](const Json& captures) {
        if(!captures.is_object())throw std::invalid_argument("invalid captures");
        for(auto it=captures.begin();it!=captures.end();++it){checkKey(it.key());counter(it.value().at("sequence"));
            it.value().at("branch").get<std::string>();bounded(it.value().at("value"),it.key().rfind("m:",0)==0?255u:65535u);}
    };
    auto checkRegisters=[&](const Json& registers) {
        if(!registers.is_object())throw std::invalid_argument("invalid registers");
        for(auto it=registers.begin();it!=registers.end();++it){checkKey("r:"+it.key());unsigned maximum=65535;for(size_t i=0;i<model.lanes.size();++i)if(it.key()==model.lanes[i].name)maximum=model.laneMaximum(i);bounded(it.value(),maximum);}
    };
    auto& h=j.at("history");counter(h.at("visit"));h.at("branch").get<std::string>();
    if(h.contains("inputPosition"))counter(h["inputPosition"]);
    if(!h.at("writers").is_object()||!h.at("values").is_object()||!h.at("views").is_object()||h["writers"].size()!=h["values"].size())throw std::invalid_argument("invalid history maps");
    for(auto it=h["writers"].begin();it!=h["writers"].end();++it){checkKey(it.key());if(!h["values"].contains(it.key()))throw std::invalid_argument("missing writer value");
        bounded(h["values"][it.key()],it.key().rfind("m:",0)==0?255u:65535u);
        counter(it.value().at("sequence"));auto inst=it.value().at("instruction").get<std::string>();if(!inst.empty()&&!j["instructions"].contains(inst))throw std::invalid_argument("unknown history supplier");
    }
    for(auto it=h["views"].begin();it!=h["views"].end();++it){if(!j["instructions"].contains(it.key()))throw std::invalid_argument("invalid history view");
        checkCaptures(it.value().at("captures"));checkRegisters(it.value().at("registers"));counter(it.value().at("lastVisit").at("sequence"));it.value().at("lastVisit").at("branch").get<std::string>();}
    for(auto it=j["annotations"].begin();it!=j["annotations"].end();++it){if(!j["instructions"].contains(it.key())||!it.value().is_array())throw std::invalid_argument("invalid annotation target");for(auto& a:it.value()){a.at("text").get<std::string>();auto kind=a.at("kind").get<std::string>();if(kind!="inference"&&kind!="confirmed"&&kind!="correction")throw std::invalid_argument("invalid annotation kind");
        if(a.contains("finding")){auto finding=a["finding"].get<std::string>();auto analysis=a.at("analysisId").get<std::string>();for(auto& id:{finding,analysis})if(id.size()!=64||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("invalid archived finding link");}
    }}

    for(auto it=j["instructions"].begin();it!=j["instructions"].end();++it){auto& n=it.value();auto bytes=n.at("bytes").get<std::string>();
        bounded(n.at("length"),model.maximumInstructionLength);bounded(n.at("address"),65535);unsigned len=n.at("length").get<unsigned>();unsigned addr=n.at("address").get<unsigned>();
        if(addr>65535||len<1||len>model.maximumInstructionLength||bytes.size()!=2*len||bytes.find_first_not_of("0123456789abcdef")!=std::string::npos||n.at("id")!=it.key())throw std::invalid_argument("invalid instruction");
        if(model.id=="gamegear"||n.contains("fetchBackings")){
            const auto& backings=n.at("fetchBackings");
            if(!backings.is_array()||backings.empty()||backings.size()>len)throw std::invalid_argument("invalid fetch backing runs");
            uint32_t next=0;uint64_t previous=0;uint32_t previousLength=0;
            for(auto& run:backings){
                bounded(run.at("offset"),len);bounded(run.at("length"),len);
                auto offset=run.at("offset").get<uint32_t>(),length=run.at("length").get<uint32_t>();auto backing=counter(run.at("location"));
                if(offset!=next||!length||length>len-next||(backing>>32)>(model.id=="gamegear"?11u:6u))throw std::invalid_argument("invalid fetch backing extent");
                if(next&&backing==previous+previousLength)throw std::invalid_argument("noncanonical fetch backing runs");
                if(!next&&run.at("location")!=n.at("location"))throw std::invalid_argument("fetch backing origin mismatch");
                next+=length;previous=backing;previousLength=length;
            }
            if(next!=len||it.key()!=hashText(n.at("location").get<std::string>()+":"+bytes+":"+backings.dump()))throw std::invalid_argument("instruction identity mismatch");
        }
        if(n.at("flow").get<std::string>().empty()||!n.at("registers").is_object()||!n.at("hardware").is_array())throw std::invalid_argument("invalid instruction metadata");
        n.at("text").get<std::string>();n.at("conditional").get<bool>();counter(n.at("location"));checkCaptures(n.at("captures"));checkRegisters(n.at("registers"));
    }
    for(auto& e:j["edges"]){if(!j["instructions"].contains(e.at("from").get<std::string>()))throw std::invalid_argument("unknown edge source");counter(e.at("targetLocation"));if(e.contains("toInstruction")&&!j["instructions"].contains(e["toInstruction"].get<std::string>()))throw std::invalid_argument("unknown observed target");}
    std::set<std::string> ids;for(auto& d:j["dependencies"]){auto id=d.at("id").get<std::string>();if(!ids.insert(id).second)throw std::invalid_argument("duplicate dependency ID");
        auto inst=d.at("instruction").get<std::string>();if(!inst.empty()&&!j["instructions"].contains(inst))throw std::invalid_argument("unknown dependency instruction");
        auto supplier=d.at("supplier").at("instruction").get<std::string>();if(!supplier.empty()&&!j["instructions"].contains(supplier))throw std::invalid_argument("unknown supplier");counter(d.at("sequence"));counter(d.at("supplier").at("sequence"));
        d.at("accepted").get<bool>();bounded(d.at("value"),65535);
        if(d.contains("origin")){auto origin=d["origin"].get<std::string>();if(origin!="unknown"&&origin!="cpu"&&origin!="device"&&origin!="dma"&&origin!="boundary"&&origin!="inspection")throw std::invalid_argument("invalid access origin");}
        if(d.contains("executionSource")){auto source=d["executionSource"].get<std::string>();if(source!="snapshot"&&source!="initialization"&&source!="bus")throw std::invalid_argument("invalid execution source");}
        if(d.contains("executionSupplier")){for(auto key:{"epoch","block","sequence"})counter(d["executionSupplier"].at(key));}
        auto key=d.at("location").get<std::string>();checkKey(key);if(key.rfind("m:",0)==0)counter(Json(key.substr(2)));else if(key.rfind("r:",0)!=0)throw std::invalid_argument("invalid dependency location");
    }
    for(auto& input:j["inputs"]){bounded(input.at("mask"),255);counter(input.at("sequence"));if(input.contains("position"))counter(input["position"]);input.at("branch").get<std::string>();}
    for(auto& gap:j["gaps"]){counter(gap.at("sequence"));gap.at("branch").get<std::string>();gap.at("reason").get<std::string>();}
    if(j.contains("boundaries")){if(!j["boundaries"].is_array())throw std::invalid_argument("invalid boundaries");
        for(auto& boundary:j["boundaries"]){counter(boundary.at("sequence"));counter(boundary.at("cycles"));boundary.at("branch").get<std::string>();
            auto kind=boundary.at("kind").get<std::string>();if(kind!="stall"&&kind!="interrupt"&&kind!="device-update")throw std::invalid_argument("invalid boundary kind");
            for(auto key:{"before","after"}){if(!boundary.at(key).is_array()||boundary[key].size()!=model.pairs.size())throw std::invalid_argument("invalid boundary registers");for(auto& value:boundary[key])bounded(value,65535);}}}
    if(j.contains("blocks")){
        if(!j["blocks"].is_array()||!j.at("graphEdges").is_array())throw std::invalid_argument("invalid graph collections");
        std::set<std::string> blocks;
        for(auto& block:j["blocks"]){auto id=block.at("id").get<std::string>();if(!blocks.insert(id).second||!j["instructions"].contains(id))throw std::invalid_argument("invalid block ID");
            if(!block.at("instructions").is_array()||block["instructions"].empty())throw std::invalid_argument("invalid block instructions");
            for(auto& instruction:block["instructions"])if(!j["instructions"].contains(instruction.get<std::string>()))throw std::invalid_argument("invalid block reference");
            if(block.at("terminal")!=block["instructions"].back())throw std::invalid_argument("invalid terminal instruction");
            checkCaptures(block.at("snapshot"));checkRegisters(block.at("registers"));
            if(!block.at("rawBytes").is_array()||!block.at("pools").is_array())throw std::invalid_argument("invalid raw snapshot");
            for(auto& value:block["rawBytes"])bounded(value,255);
            uint64_t previousOffset=0;bool first=true;
            for(auto& pool:block["pools"]){if(!pool.is_array()||pool.size()!=2)throw std::invalid_argument("invalid snapshot pool");
                checkKey("m:"+pool[0].get<std::string>());auto offset=counter(pool[1]);
                if(offset>=block["rawBytes"].size()||(!first&&offset<=previousOffset)||(first&&offset!=0))throw std::invalid_argument("invalid snapshot range");
                first=false;previousOffset=offset;}
            if(block["pools"].empty()!=block["rawBytes"].empty())throw std::invalid_argument("missing snapshot pool");
        }
        for(auto& edge:j["graphEdges"]){auto from=edge.at("from").get<std::string>(),to=edge.at("to").get<std::string>();
            if(!blocks.contains(from)||(!blocks.contains(to)&&to.rfind("unresolved:",0)!=0))throw std::invalid_argument("invalid graph edge");}
    }
    if(j.contains("transfers")){
        if(!j["transfers"].is_array())throw std::invalid_argument("invalid transfers");
        std::set<std::string> ids;
        for(auto& t:j["transfers"]){if(!ids.insert(t.at("id").get<std::string>()).second)throw std::invalid_argument("duplicate transfer");
            auto inst=t.at("instruction").get<std::string>();if(!inst.empty()&&!j["instructions"].contains(inst))throw std::invalid_argument("invalid transfer instruction");
            if(t.contains("nextInstruction")&&!j["instructions"].contains(t["nextInstruction"].get<std::string>()))throw std::invalid_argument("invalid next instruction");
            t.at("branch").get<std::string>();counter(t.at("sequence"));t.at("taken").get<bool>();t.at("root").get<bool>();
            for(auto key:{"targetLocation","fallthroughLocation"})if((counter(t.at(key))>>32)>(model.id=="gamegear"?11:6))throw std::invalid_argument("invalid transfer location");
            auto f=t.at("flow").get<std::string>();if(f!="fallthrough"&&f!="branch"&&f!="call"&&f!="return"&&f!="indirect"&&f!="halt"&&f!="stop"&&f!="interrupt")throw std::invalid_argument("invalid transfer flow");
        }
    }
    if(j.contains("captureWindows")){auto& c=j["captureWindows"];if(c.at("version")!=1||!c.at("windows").is_array()||!c.at("intentionalOmissions").is_array())throw std::invalid_argument("invalid capture window metadata");
        std::set<std::string> windows;for(auto& w:c["windows"]){auto id=w.at("id").get<std::string>();if(id.empty()||!windows.insert(id).second||std::find(j["sessions"].begin(),j["sessions"].end(),Json(id))==j["sessions"].end())throw std::invalid_argument("duplicate/unknown capture window");counter(w.at("startStep"));counter(w.at("waitSteps"));bounded(w.at("count"),128);if(w["count"]==0)throw std::invalid_argument("empty capture window");}
        for(auto& o:c["intentionalOmissions"]){if(counter(o.at("toStep"))<=counter(o.at("fromStep")))throw std::invalid_argument("invalid intentional omission");o.at("reason").get<std::string>();if(!windows.contains(o.at("captureId").get<std::string>()))throw std::invalid_argument("unknown omission capture identity");}}
    if(j.contains("analysis"))validateAnalysis(j);
    validatePorting(j);
    validateMeaning(j);
    if(j.contains("porting")&&j.dump().size()>defaultBudget/2)throw std::invalid_argument("port accounting exceeds analysis state budget");
    if(j.dump().size()>defaultBudget*2)throw std::invalid_argument("project budget exceeded");
}
Project Project::load(const std::filesystem::path& path,const std::string& expected){
    auto j=read(path);validate(j);auto hash=j["romSha256"].get<std::string>();if(!expected.empty()&&hash!=expected)throw std::invalid_argument("project ROM mismatch");
    Project p(hash,defaultBudget,j["core"].get<std::string>());p.state_=j;p.state_.erase("blocks");p.state_.erase("graphEdges");p.revision_=counter(j["revision"]);
    p.views_=j.at("history").at("views");p.writers_=j["history"]["writers"];p.values_=j["history"]["values"];
    p.branch_=j["history"]["branch"].get<std::string>();p.visit_=counter(j["history"]["visit"]);p.inputPosition_=j["history"].contains("inputPosition")?counter(j["history"]["inputPosition"]):0;
    p.used_=j.dump().size(); // imported history is analysis only; never resumes execution
    return p;
}
void Project::merge(const Json& j){
    validate(j);if(j["core"]!=state_["core"]||j["romSha256"]!=state_["romSha256"])throw std::invalid_argument("project ROM mismatch");
    auto next=state_;
    for(auto key:{"symbolImports","purposeClaims"})if(j.contains(key))for(auto it=j[key].begin();it!=j[key].end();++it){
        if(next.contains(key)&&next[key].contains(it.key())&&next[key][it.key()]!=it.value())throw std::invalid_argument("conflicting meaning metadata");
        next[key][it.key()]=it.value();
    }
    if(!next.contains("transfers"))next["transfers"]=Json::array();
    std::map<std::string,size_t> transferIndex;
    for(size_t i=0;i<next["transfers"].size();++i)transferIndex[next["transfers"][i]["id"].get<std::string>()]=i;
    if(j.contains("transfers"))for(auto& v:j["transfers"]){auto id=v["id"].get<std::string>();
        if(!transferIndex.contains(id)){transferIndex[id]=next["transfers"].size();next["transfers"].push_back(v);}
        else {auto& existing=next["transfers"][transferIndex[id]];if(existing==v)continue;
            auto left=existing,right=v;left.erase("nextInstruction");right.erase("nextInstruction");
            if(left!=right||(existing.contains("nextInstruction")&&v.contains("nextInstruction")&&existing["nextInstruction"]!=v["nextInstruction"]))throw std::invalid_argument("conflicting transfer evidence");
            if(v.contains("nextInstruction"))existing["nextInstruction"]=v["nextInstruction"];}
    }
    if(!next.contains("boundaries"))next["boundaries"]=Json::array();
    if(j.contains("boundaries"))for(auto& v:j["boundaries"])if(std::find(next["boundaries"].begin(),next["boundaries"].end(),v)==next["boundaries"].end())next["boundaries"].push_back(v);
    for(auto it=j["instructions"].begin();it!=j["instructions"].end();++it)if(!next["instructions"].contains(it.key()))next["instructions"][it.key()]=it.value();
    std::map<std::string,size_t> dependencyIndex;
    for(size_t i=0;i<next["dependencies"].size();++i)dependencyIndex[next["dependencies"][i]["id"].get<std::string>()]=i;
    for(auto& d:j["dependencies"]){auto id=d["id"].get<std::string>();if(dependencyIndex.contains(id)){if(next["dependencies"][dependencyIndex[id]]!=d)throw std::invalid_argument("conflicting dependency evidence");}
        else {dependencyIndex[id]=next["dependencies"].size();next["dependencies"].push_back(d);}}
    for(const char* key:{"edges","gaps","inputs","sessions"}){std::set<std::string> existing;for(auto& v:next[key])existing.insert(v.dump());
        for(auto& v:j.at(key))if(existing.insert(v.dump()).second)next[key].push_back(v);}
    for(auto it=j["annotations"].begin();it!=j["annotations"].end();++it){auto& values=next["annotations"][it.key()];if(values.is_null())values=Json::array();for(auto& v:it.value())if(std::find(values.begin(),values.end(),v)==values.end())values.push_back(v);}
    if(j.contains("captureWindows")){if(!next.contains("captureWindows"))next["captureWindows"]=j["captureWindows"];else{for(auto key:{"windows","intentionalOmissions"})for(auto& v:j["captureWindows"][key]){auto& entries=next["captureWindows"][key];if(std::find(entries.begin(),entries.end(),v)==entries.end())entries.push_back(v);}}}
    if(j.contains("porting"))next["porting"]=next.contains("porting")?mergePorting(next["porting"],j["porting"]):j["porting"];
    if(next==state_)return;
    if(evidenceDigest(next)!=evidenceDigest(state_))next.erase("analysis");
    auto validation=next;validation["revision"]=decimal(revision_);validation["history"]=history();
    if(validation.contains("analysis")){auto captured=document();validation["blocks"]=std::move(captured["blocks"]);validation["graphEdges"]=std::move(captured["graphEdges"]);}
    validate(validation);
    auto charge=next.dump().size()*2;if(charge>budget_)throw std::invalid_argument("merge exceeds analysis budget");
    auto growth=charge>used_?charge-used_:0;
    if(sharedBudget_&&!sharedBudget_->reserve(growth))throw std::invalid_argument("merge exceeds shared analysis budget");
    // Existing conservative reservations are retained; merges never undercharge them.
    state_=std::move(next);used_=std::max(used_,charge);++revision_;
}
void Project::annotate(const std::string& instruction,const Json& annotation){
    if(!state_["instructions"].contains(instruction))throw std::invalid_argument("annotation requires a known instruction");
    auto kind=annotation.at("kind").get<std::string>(),text=annotation.at("text").get<std::string>();
    if((kind!="inference"&&kind!="confirmed"&&kind!="correction")||text.empty()||text.size()>65536)throw std::invalid_argument("invalid annotation");
    auto entries=state_["annotations"].value(instruction,Json::array());
    if(std::find(entries.begin(),entries.end(),annotation)!=entries.end())return;
    auto charge=annotation.dump().size()+512;
    if(charge>budget_-std::min(budget_,used_)||(sharedBudget_&&!sharedBudget_->reserve(charge)))throw std::invalid_argument("annotation exceeds budget");
    used_+=charge;entries.push_back(annotation);state_["annotations"][instruction]=std::move(entries);++revision_;
}
void Project::save(const std::filesystem::path& path)const{write(path,document());}
}
