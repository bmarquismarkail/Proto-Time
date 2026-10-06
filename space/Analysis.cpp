#include "Analysis.hpp"
#include "Porting.hpp"
#include "cores/gameboy/hardware_registers.hpp"
#include <map>
#include <set>
#include <tuple>
#include <deque>
#include <algorithm>
namespace BMMQ::Space {
namespace {
std::string hash(const std::string& s){return digest(std::span(reinterpret_cast<const uint8_t*>(s.data()),s.size()));}
using Strings=std::set<std::string>;
using Graph=std::map<std::string,Strings>;
struct Exhausted{};
struct Budget {size_t left;void use(size_t n){if(n>left)throw Exhausted{};left-=n;}};
std::vector<uint8_t> bytes(const Json& n){auto s=n.at("bytes").get<std::string>();std::vector<uint8_t> out;
    for(size_t i=0;i<s.size();i+=2)out.push_back(static_cast<uint8_t>(std::stoul(s.substr(i,2),nullptr,16)));
    return out;}
std::string readOperand(const Json& instruction){
    auto b=bytes(instruction);unsigned op=b[0];constexpr const char* regs[]={"r:B","r:C","r:D","r:E","r:H","r:L","memory","r:A"};
    if(op>=0x40&&op<0x80&&op!=0x76)return regs[op&7];
    switch(op){case 0x02:case 0x12:case 0x22:case 0x32:case 0xe0:case 0xe2:case 0xea:return "r:A";
        case 0x0a:case 0x1a:case 0x2a:case 0x3a:case 0xf0:case 0xf2:case 0xfa:return "memory";default:return "";}
}
std::string writeOperand(const Json& instruction){
    auto b=bytes(instruction);unsigned op=b[0];constexpr const char* regs[]={"r:B","r:C","r:D","r:E","r:H","r:L","memory","r:A"};
    if(op>=0x40&&op<0x80&&op!=0x76)return regs[(op>>3)&7];
    switch(op){case 0x02:case 0x12:case 0x22:case 0x32:case 0xe0:case 0xe2:case 0xea:return "memory";
        case 0x0a:case 0x1a:case 0x2a:case 0x3a:case 0xf0:case 0xf2:case 0xfa:return "r:A";default:return "";}
}
bool operand(const std::string& expected,const Json& d){auto location=d.at("location").get<std::string>();return expected=="memory"?location.starts_with("m:"):location==expected;}
using Visit=std::pair<std::string,std::string>;
using Writer=std::tuple<std::string,std::string,std::string,std::string>;
Writer writerKey(const Json& d,bool supplier){auto& s=supplier?d.at("supplier"):d;return {s.at("branch").get<std::string>(),s.at("sequence").get<std::string>(),s.at("instruction").get<std::string>(),d.at("location").get<std::string>()};}
bool cpu(const Json& d){return d.value("origin",std::string("unknown"))=="cpu"&&d.at("accepted").get<bool>()&&!d.at("instruction").get<std::string>().empty();}
void references(const Json& list,const Strings& ids){if(!list.is_array())throw std::invalid_argument("invalid analysis references");for(auto& x:list)if(!x.is_string()||!ids.contains(x.get<std::string>()))throw std::invalid_argument("unknown analysis reference");}
size_t bounded(const Json& request,const char* key,size_t fallback,size_t max){if(!request.contains(key))return fallback;auto& n=request[key];if(!n.is_number_integer()||n<0||n>max)throw std::invalid_argument(std::string("invalid ")+key);return n.get<size_t>();}
}
Json hardwareDescriptor(uint64_t loc,bool write){
    auto space=loc>>32;auto a=uint16_t(loc);std::string category,role,name;
    if(space==1&&write){category="cartridge.mapping";role="bank.control";}
    else if(space==4){if(a>=0x8000&&a<=0x97ff){category="graphics.tile-data";role="graphics.tile-data-output";}
        else if(a>=0x9800&&a<=0x9fff){category="graphics.tile-map";role="graphics.tile-map-output";}
        else if(a>=0xfe00&&a<=0xfe9f){category="graphics.sprites";role="graphics.sprite-output";}}
    else if(space==5||space==6){
        for(auto& spec:GB::HardwareRegisters::kSpecs)if(spec.address==a)name=spec.name;
        if(a==0xff00){category="input";role=write?"input.selection":"input.sampling";}
        else if(a==0xff01||a==0xff02){category="serial";role="serial.io";}
        else if(a>=0xff04&&a<=0xff07){category="timer";role="timer.configuration";}
        else if(a==0xff0f||a==0xffff){category="interrupt";role="interrupt.configuration";}
        else if(a>=0xff10&&a<=0xff26){category="audio.channel";role="audio.configuration";}
        else if(a>=0xff30&&a<=0xff3f){category="audio.wave";role="audio.wave-output";}
        else if(a==0xff46||(a>=0xff51&&a<=0xff55)){category="dma";role="dma.request";}
        else if((a>=0xff40&&a<=0xff4b)||(a>=0xff68&&a<=0xff6b)){category="graphics.display";role="display.configuration";}
        else if(a==0xff4f||a==0xff70||a==0xff50){category="mapping";role="bank.control";}
    }
    if(category.empty())return nullptr;
    const bool cgbOnly=a==0xff4f||a==0xff70||(a>=0xff51&&a<=0xff55)||(a>=0xff68&&a<=0xff6b);
    if(cgbOnly)role="";
    // Reads of output/configuration registers are facts, not evidence of configuration.
    if(!write&&category!="input"&&category!="serial")role="";
    return {{"category",category},{"role",role},{"name",name},{"address",a},{"location",decimal(loc)},{"model",cgbOnly?"cgb-specific / unassessed in DMG":"dmg/shared"}};
}
std::string evidenceDigest(const Json& p){Json facts;
    for(auto key:{"romSha256","core","instructions","edges","dependencies","gaps","transfers","boundaries"})if(p.contains(key))facts[key]=p[key];
    return hash(facts.dump());
}
Json analyzeProject(Json p,size_t limit){
    if(limit>Project::defaultBudget)throw std::invalid_argument("analysis budget exceeds project limit");
    Project::validate(p);p.erase("analysis");
    auto source=evidenceDigest(p);auto identity=hash(p.at("romSha256").get<std::string>()+":"+analyzerVersion+":"+p.at("revision").get<std::string>()+":"+source);
    Json a={{"schemaVersion",1},{"analyzerVersion",analyzerVersion},{"id",identity},{"romSha256",p["romSha256"]},{"core",p["core"]},
        {"sourceRevision",p["revision"]},{"evidenceDigest",source},{"incomplete",false},{"limitations",Json::array({"Execution-driven evidence; unvisited paths and gameplay meaning remain unassessed."})}};
    for(auto key:{"routines","loops","findings","hardwareFacts","dataTransfers","dependencyEdges"})a[key]=Json::array();
    if(!p["gaps"].empty()){a["incomplete"]=true;a["limitations"].push_back("Capture contains gaps; conclusions cover retained evidence only.");}
    if(!p.contains("transfers")){a["incomplete"]=true;a["limitations"].push_back("Legacy capture lacks observed control-flow outcomes and access origins.");}
    auto unknown=std::count_if(p["dependencies"].begin(),p["dependencies"].end(),[](auto& d){return d.value("origin",std::string("unknown"))=="unknown";});
    if(unknown){a["incomplete"]=true;a["limitations"].push_back("Unknown origins in "+std::to_string(unknown)+" access records; excluded from CPU-role inference.");}
    try {
        Budget budget{limit};budget.use(p.dump().size()*2);
        Graph graph,reverse;std::map<std::string,std::string> at;Strings ambiguous,entries;
        for(auto it=p["instructions"].begin();it!=p["instructions"].end();++it){budget.use(768);graph[it.key()];reverse[it.key()];auto loc=it.value()["location"].get<std::string>();if(at.contains(loc))ambiguous.insert(loc);at[loc]=it.key();}
        auto resolve=[&](const Json& t){if(t.contains("nextInstruction"))return t["nextInstruction"].get<std::string>();auto loc=t["targetLocation"].get<std::string>();return at.contains(loc)&&!ambiguous.contains(loc)?at[loc]:std::string();};
        std::map<std::string,Strings> calls;std::map<std::string,Json> callEvidence;
        for(auto& t:p.value("transfers",Json::array())){budget.use(384);auto from=t["instruction"].get<std::string>(),to=resolve(t);auto flow=t["flow"].get<std::string>();
            if(t["root"]&&!from.empty())entries.insert(from);
            if(flow=="interrupt"&&!to.empty())entries.insert(to);
            if(flow=="call"&&t["taken"]&&!to.empty()){entries.insert(to);calls[from].insert(to);if(callEvidence[from].is_null())callEvidence[from]=Json::array();callEvidence[from].push_back(t["id"]);}
        }
        // Ordinary intra-routine edges exclude call targets and returns.
        for(auto& e:p["edges"]){auto from=e["from"].get<std::string>(),flow=p["instructions"][from]["flow"].get<std::string>();
            auto loc=e["targetLocation"].get<std::string>();auto to=e.contains("toInstruction")?e["toInstruction"].get<std::string>():at.contains(loc)&&!ambiguous.contains(loc)?at[loc]:std::string();
            if(to.empty()){a["limitations"].push_back("Unresolved destination from "+from);continue;}
            if((flow=="return"&&e["kind"]!="fallthrough")||flow=="halt"||flow=="stop"||(flow=="call"&&e["kind"]!="fallthrough"))continue;
            budget.use(256);graph[from].insert(to);reverse[to].insert(from);
        }
        if(entries.empty())for(auto& [id,_]:graph)if(reverse[id].empty())entries.insert(id);
        if(entries.empty()&&!graph.empty())entries.insert(graph.begin()->first);
        // Iterative Kosaraju: finite cyclic captures do not recurse on the host stack.
        Strings visited;std::vector<std::string> order;
        for(auto& [root,_]:graph)if(!visited.contains(root)){std::vector<std::pair<std::string,bool>> todo{{root,false}};
            while(!todo.empty()){auto [id,done]=todo.back();todo.pop_back();if(done){order.push_back(id);continue;}if(!visited.insert(id).second)continue;budget.use(64);todo.emplace_back(id,true);
                for(auto it=graph[id].rbegin();it!=graph[id].rend();++it)if(!visited.contains(*it))todo.emplace_back(*it,false);}}
        visited.clear();std::map<std::string,size_t> loopOf;
        for(auto it=order.rbegin();it!=order.rend();++it)if(visited.insert(*it).second){Strings members;std::vector<std::string> todo{*it};
            while(!todo.empty()){auto id=todo.back();todo.pop_back();budget.use(128);members.insert(id);for(auto& next:reverse[id])if(visited.insert(next).second)todo.push_back(next);}
            if(members.size()==1&&!graph[*members.begin()].contains(*members.begin()))continue;
            Json loop={{"id",hash(Json(members).dump())},{"instructions",members},{"entries",Json::array()},{"exits",Json::array()},{"transfers",Json::array()},{"repetition",Json::object()},
                {"limitations",Json::array({"Structural cycle; repetition is not an iteration count or optimization contract."})}};
            for(auto& member:members){loopOf[member]=a["loops"].size();for(auto& from:reverse[member])if(!members.contains(from))loop["entries"].push_back({from,member});for(auto& to:graph[member])if(!members.contains(to))loop["exits"].push_back({member,to});}
            std::map<std::string,std::map<std::string,size_t>> counts;
            for(auto& t:p.value("transfers",Json::array()))if(members.contains(t["instruction"].get<std::string>())){loop["transfers"].push_back(t["id"]);++counts[t["branch"].get<std::string>()][t["instruction"].get<std::string>()];}
            for(auto& [branch,c]:counts){size_t largest=0;for(auto& [_,n]:c)largest=std::max(largest,n);loop["repetition"][branch]=decimal(largest);}
            budget.use(loop.dump().size()*2);a["loops"].push_back(std::move(loop));
        }
        std::map<std::string,const Json*> dependencies;std::map<Visit,std::vector<const Json*>> visits;std::map<Writer,std::vector<std::string>> writers;
        for(auto& d:p["dependencies"]){budget.use(1024);auto id=d["id"].get<std::string>();dependencies[id]=&d;visits[{d["branch"].get<std::string>(),d["sequence"].get<std::string>()}].push_back(&d);
            if(cpu(d)&&d["access"]=="write"&&d["kind"]!="mapping")writers[writerKey(d,false)].push_back(id);
        }
        std::map<std::string,std::string> ancestry;
        for(auto& [id,d]:dependencies)if(cpu(*d)&&(*d)["access"]=="read"){auto w=writers.find(writerKey(*d,true));if(w!=writers.end()&&w->second.size()==1){ancestry[id]=w->second.front();a["dependencyEdges"].push_back({{"from",id},{"to",w->second.front()},{"kind","supplier"}});}}
        for(auto& [_,group]:visits){if(group.empty())continue;auto inst=(*group.front())["instruction"].get<std::string>();if(inst.empty())continue;
            auto src=readOperand(p["instructions"][inst]),dst=writeOperand(p["instructions"][inst]);if(src.empty())continue;
            std::vector<const Json*> reads;for(auto* d:group)if(cpu(*d)&&(*d)["access"]=="read"&&operand(src,*d))reads.push_back(d);
            if(reads.size()!=1)continue;
            for(auto* d:group)if(cpu(*d)&&(*d)["access"]=="write"&&operand(dst,*d)&&(*d)["value"]==(*reads.front())["value"]){
                auto id=(*d)["id"].get<std::string>(),sourceId=(*reads.front())["id"].get<std::string>();ancestry[id]=sourceId;
                a["dependencyEdges"].push_back({{"from",id},{"to",sourceId},{"kind","copy"}});}
        }
        std::map<std::pair<std::string,std::string>,Json> findings;
        auto finding=[&](const Json& d,const std::string& role,const std::string& rule){auto inst=d["instruction"].get<std::string>();auto& f=findings[{inst,role}];
            if(f.is_null())f={{"id",hash(inst+":"+role)},{"instruction",inst},{"role",role},{"rule",rule},{"dependencies",Json::array()},{"transfers",Json::array()},{"branches",Json::array()},
                {"limitations",Json::array({"Observed hardware role; gameplay purpose is not established."})}};
            f["dependencies"].push_back(d["id"]);auto branch=d["branch"];if(std::find(f["branches"].begin(),f["branches"].end(),branch)==f["branches"].end())f["branches"].push_back(branch);};
        std::map<Visit,size_t> inputCounts;
        for(auto& d:p["dependencies"]){auto key=d["location"].get<std::string>();if(!key.starts_with("m:"))continue;auto descriptor=hardwareDescriptor(counter(Json(key.substr(2))),d["access"]=="write");
            if(!descriptor.is_null()){budget.use(768);a["hardwareFacts"].push_back({{"dependency",d["id"]},{"descriptor",descriptor},{"origin",d.value("origin",std::string("unknown"))},{"accepted",d["accepted"]},{"access",d["access"]}});}
            if(!cpu(d))continue;
            auto role=descriptor.is_null()?std::string():descriptor["role"].get<std::string>();if(!role.empty())finding(d,role,"accepted-cpu-hardware-access");
            auto inst=d["instruction"].get<std::string>();if(role=="input.sampling"&&loopOf.contains(inst))++inputCounts[{d["branch"].get<std::string>(),inst}];
            if(d["access"]!="write")continue;
            std::string cur=d["id"].get<std::string>(),sourceId;Strings seen;Json path=Json::array();bool truncated=false;
            while(seen.insert(cur).second){if(path.size()>=64){truncated=true;break;}path.push_back(cur);auto& item=*dependencies.at(cur);if(item["access"]=="read"&&item["location"].get<std::string>().starts_with("m:"))sourceId=cur;
                if(!ancestry.contains(cur))break;
                cur=ancestry[cur];}
            if(!sourceId.empty()){Json transfer={{"id",hash(d["id"].get<std::string>()+":"+sourceId)},{"source",sourceId},{"target",d["id"]},{"path",path},{"branch",d["branch"]},{"truncated",truncated},
                {"limitations",Json::array({"Load-only observed path; unsupported transformations terminate provenance."})}};budget.use(transfer.dump().size()*2);a["dataTransfers"].push_back(std::move(transfer));}
        }
        for(auto& d:p["dependencies"]){auto inst=d["instruction"].get<std::string>();if(cpu(d)&&loopOf.contains(inst)&&d["access"]=="read"&&d["location"]=="m:"+decimal(location(6,0,0xff00))&&inputCounts[{d["branch"].get<std::string>(),inst}]>1){
            finding(d,"input.polling","repeated-input-read-in-structural-cycle");auto& f=findings[{inst,"input.polling"}];f["transfers"]=a["loops"][loopOf[inst]]["transfers"];}}
        for(auto& [_,f]:findings){budget.use(f.dump().size()*2);a["findings"].push_back(f);}
        Graph routineCalls;std::map<std::string,Strings> body;
        for(auto& entry:entries){Strings members;std::vector<std::string> todo{entry};
            while(!todo.empty()){auto id=todo.back();todo.pop_back();if(!members.insert(id).second)continue;budget.use(512);
                for(auto& next:graph[id])if(!entries.contains(next)||next==entry)todo.push_back(next);
                for(auto& callee:calls[id])routineCalls[entry].insert(callee);}
            body[entry]=members;}
        for(auto& [entry,members]:body){Json routine={{"id",entry},{"entry",entry},{"instructions",members},{"blocks",Json::array()},{"callees",routineCalls[entry]},{"callers",Json::array()},{"callEvidence",Json::array()},
                {"directFindings",Json::array()},{"calleeFindings",Json::array()},{"sharedInstructions",Json::array()},{"boundaryConflicts",Json::array()},{"limitations",Json::array({"Routine candidate, not proof of exclusive ownership or complete reachability.","Callee effects aggregate observed behaviors and need not occur in the same call or branch."})}};
            for(auto& block:p["blocks"])for(auto& inst:block["instructions"])if(members.contains(inst.get<std::string>())){routine["blocks"].push_back(block["id"]);break;}
            for(auto& [caller,callees]:routineCalls)if(callees.contains(entry))routine["callers"].push_back(caller);
            for(auto& inst:members){for(auto& next:graph[inst])if(entries.contains(next)&&next!=entry)routine["boundaryConflicts"].push_back({{"from",inst},{"to",next},{"reason","ordinary edge reaches another routine entry"}});if(callEvidence.contains(inst))for(auto& t:callEvidence[inst])routine["callEvidence"].push_back(t);
                size_t owners=0;for(auto& [_,other]:body)owners+=other.contains(inst);if(owners>1)routine["sharedInstructions"].push_back(inst);}
            Strings reached;std::vector<std::string> todo(routineCalls[entry].begin(),routineCalls[entry].end());
            while(!todo.empty()){auto id=todo.back();todo.pop_back();if(!reached.insert(id).second)continue;budget.use(128);for(auto& next:routineCalls[id])todo.push_back(next);}
            for(auto& f:a["findings"]){auto inst=f["instruction"].get<std::string>();if(members.contains(inst))routine["directFindings"].push_back(f["id"]);
                else for(auto& callee:reached)if(body[callee].contains(inst)){routine["calleeFindings"].push_back(f["id"]);break;}}
            budget.use(routine.dump().size()*2);a["routines"].push_back(std::move(routine));}
        a["chargedBytes"]=decimal(limit-budget.left);
    }catch(const Exhausted&){for(auto key:{"routines","loops","findings","hardwareFacts","dataTransfers","dependencyEdges"})a[key]=Json::array();a["incomplete"]=true;a["limitations"].push_back("Analysis resource budget exhausted; no partial assessment is published.");a["chargedBytes"]=decimal(limit);}
    p["analysis"]=std::move(a);validateAnalysis(p);return p;
}
void validateAnalysis(const Json& p){
    auto& a=p.at("analysis");
    if(a.at("schemaVersion")!=1||a.at("analyzerVersion")!=analyzerVersion||a.at("romSha256")!=p.at("romSha256")||a.at("core")!=p.at("core"))throw std::invalid_argument("unsupported analysis identity");
    auto rev=counter(a.at("sourceRevision"));if(rev>counter(p.at("revision"))||a.at("evidenceDigest")!=evidenceDigest(p))throw std::invalid_argument("stale analysis evidence");
    if(a.at("id")!=hash(p.at("romSha256").get<std::string>()+":"+analyzerVersion+":"+a.at("sourceRevision").get<std::string>()+":"+a.at("evidenceDigest").get<std::string>()))throw std::invalid_argument("invalid analysis ID");
    a.at("incomplete").get<bool>();if(a.contains("retainedBytes")&&counter(a["retainedBytes"])>Project::defaultBudget)throw std::invalid_argument("invalid retained storage");if(!a.at("limitations").is_array()||counter(a.at("chargedBytes"))>Project::defaultBudget)throw std::invalid_argument("invalid analysis limits");
    for(auto& x:a["limitations"])x.get<std::string>();
    Strings instructions,blocks,deps,transfers,routines,loops,findings,branches;
    for(auto it=p.at("instructions").begin();it!=p["instructions"].end();++it)instructions.insert(it.key());
    for(auto& b:p.at("blocks"))blocks.insert(b.at("id").get<std::string>());
    for(auto& d:p.at("dependencies")){deps.insert(d.at("id").get<std::string>());branches.insert(d.at("branch").get<std::string>());}
    for(auto& t:p.value("transfers",Json::array())){transfers.insert(t.at("id").get<std::string>());branches.insert(t.at("branch").get<std::string>());}
    for(auto& b:p.value("boundaries",Json::array()))branches.insert(b.at("branch").get<std::string>());
    for(auto key:{"routines","loops","findings","hardwareFacts","dataTransfers","dependencyEdges"})if(!a.at(key).is_array())throw std::invalid_argument("invalid analysis collection");
    auto collect=[](const Json& list,Strings& ids){for(auto& x:list)if(!ids.insert(x.at("id").get<std::string>()).second)throw std::invalid_argument("duplicate analysis ID");};
    collect(a["routines"],routines);collect(a["loops"],loops);collect(a["findings"],findings);
    std::map<std::string,const Json*> dependency;for(auto& d:p["dependencies"])dependency[d["id"].get<std::string>()]=&d;
    for(auto& f:a["findings"]){if(!instructions.contains(f.at("instruction").get<std::string>()))throw std::invalid_argument("invalid finding instruction");
        references(f.at("dependencies"),deps);references(f.at("transfers"),transfers);if(f["dependencies"].empty())throw std::invalid_argument("unsupported finding");
        auto rule=f.at("rule").get<std::string>();if(rule!="accepted-cpu-hardware-access"&&rule!="repeated-input-read-in-structural-cycle")throw std::invalid_argument("unknown finding rule");
        for(auto& id:f["dependencies"]){auto& d=*dependency.at(id.get<std::string>());if(!cpu(d)||d["instruction"]!=f["instruction"])throw std::invalid_argument("finding has non-CPU evidence");
            auto key=d["location"].get<std::string>();if(!key.starts_with("m:"))throw std::invalid_argument("finding requires hardware evidence");
            auto h=hardwareDescriptor(counter(Json(key.substr(2))),d["access"]=="write");
            if(h.is_null()||(rule=="accepted-cpu-hardware-access"?h["role"]!=f["role"]:h["role"]!="input.sampling"||f["role"]!="input.polling"))throw std::invalid_argument("finding role differs from hardware evidence");}
        references(f.at("branches"),branches);
        for(auto& id:f["dependencies"])if(std::find(f["branches"].begin(),f["branches"].end(),dependency.at(id.get<std::string>())->at("branch"))==f["branches"].end())throw std::invalid_argument("missing finding origin branch");
        f.at("role").get<std::string>();if(!f.at("limitations").is_array())throw std::invalid_argument("invalid finding metadata");}
    for(auto& r:a["routines"]){if(r.at("entry")!=r.at("id")||!instructions.contains(r["entry"].get<std::string>()))throw std::invalid_argument("invalid routine entry");
        references(r.at("instructions"),instructions);references(r.at("blocks"),blocks);references(r.at("callers"),routines);references(r.at("callees"),routines);
        if(r.contains("boundaryConflicts"))for(auto& c:r["boundaryConflicts"])if(!instructions.contains(c.at("from").get<std::string>())||!instructions.contains(c.at("to").get<std::string>()))throw std::invalid_argument("invalid routine boundary reference");
        references(r.at("callEvidence"),transfers);references(r.at("sharedInstructions"),instructions);references(r.at("directFindings"),findings);references(r.at("calleeFindings"),findings);}
    for(auto& l:a["loops"]){references(l.at("instructions"),instructions);references(l.at("transfers"),transfers);
        for(auto key:{"entries","exits"}){if(!l.at(key).is_array())throw std::invalid_argument("invalid cycle edges");for(auto& e:l[key]){if(e.size()!=2)throw std::invalid_argument("invalid cycle edge");references(e,instructions);}}
        if(!l.at("repetition").is_object())throw std::invalid_argument("invalid repetition");
        for(auto it=l["repetition"].begin();it!=l["repetition"].end();++it){if(!branches.contains(it.key()))throw std::invalid_argument("unknown repetition branch");counter(it.value());}}
    for(auto& f:a["hardwareFacts"]){auto id=f.at("dependency").get<std::string>();if(!deps.contains(id))throw std::invalid_argument("invalid hardware fact");
        auto& d=*dependency.at(id);auto key=d.at("location").get<std::string>();if(!key.starts_with("m:")||f.at("descriptor")!=hardwareDescriptor(counter(Json(key.substr(2))),d["access"]=="write")||f.at("origin")!=d.value("origin",std::string("unknown"))||f.at("accepted")!=d["accepted"]||f.at("access")!=d["access"])throw std::invalid_argument("hardware fact differs from evidence");}
    Strings dataIds;collect(a["dataTransfers"],dataIds);
    for(auto& t:a["dataTransfers"]){if(!deps.contains(t.at("source").get<std::string>())||!deps.contains(t.at("target").get<std::string>()))throw std::invalid_argument("invalid data transfer");references(t.at("path"),deps);
        if(t["path"].empty()||t["path"][0]!=t["target"]||std::find(t["path"].begin(),t["path"].end(),t["source"])==t["path"].end()||t.at("branch")!=dependency.at(t["target"].get<std::string>())->at("branch"))throw std::invalid_argument("invalid transfer provenance");
        t.at("truncated").get<bool>();}
    for(auto& e:a["dependencyEdges"]){if(!deps.contains(e.at("from").get<std::string>())||!deps.contains(e.at("to").get<std::string>())||(e.at("kind")!="copy"&&e["kind"]!="supplier"))throw std::invalid_argument("invalid dependency edge");}
}
Json queryAnalysis(const Json& p,const Json& request){
    auto port=request.value("type",std::string());
    if(port=="port"||port=="inventory"||port=="conversions"||port=="obligations"||port=="verification")return queryPorting(p,request);
    if(!p.contains("analysis"))throw std::invalid_argument("capture is not analyzed; run analyze explicitly");
    validateAnalysis(p);auto& a=p["analysis"];
    if(request.contains("analysisId")&&request["analysisId"]!=a["id"])throw std::invalid_argument("query analysis identity mismatch");
    auto type=request.value("type",std::string("summary"));std::vector<const Json*> items;std::deque<Json> owned;
    auto add=[&](const Json& value){items.push_back(&value);};
    auto own=[&](Json value){owned.push_back(std::move(value));add(owned.back());};
    bool depthLimited=false,resourceLimited=false;
    auto id=request.value("id",std::string());
    auto collection=[&](const char* key){for(auto& x:a[key])if(id.empty()||x["id"]==id)add(x);};
    if(type=="summary")own({{"routines",a["routines"].size()},{"loops",a["loops"].size()},{"findings",a["findings"].size()},{"hardwareFacts",a["hardwareFacts"].size()},{"dataTransfers",a["dataTransfers"].size()},{"limitations",a["limitations"]}});
    else if(type=="routines"||type=="routine")collection("routines");
    else if(type=="loops")collection("loops");
    else if(type=="findings"){auto role=request.value("role",std::string());for(auto& f:a["findings"])if((id.empty()||f["id"]==id)&&(role.empty()||f["role"]==role))add(f);}
    else if(type=="blocks"){for(auto& b:p.at("blocks"))if(id.empty()||b["id"]==id)add(b);}
    else if(type=="callers"||type=="callees"){
        auto found=std::find_if(a["routines"].begin(),a["routines"].end(),[&](auto& r){return r["id"]==id;});if(found==a["routines"].end())throw std::invalid_argument("unknown routine");
        for(auto& target:(*found)[type])for(auto& r:a["routines"])if(r["id"]==target)add(r);
    }else if(type=="predecessors"||type=="successors"){
        if(std::none_of(p["blocks"].begin(),p["blocks"].end(),[&](auto& b){return b["id"]==id;}))throw std::invalid_argument("unknown block");
        for(auto& e:p["graphEdges"])if(e[type=="predecessors"?"to":"from"]==id)add(e);
    }else if(type=="hardware"){
        auto category=request.value("category",std::string());for(auto& f:a["hardwareFacts"])if((id.empty()||f["dependency"]==id)&&(category.empty()||f["descriptor"]["category"]==category))add(f);
    }else if(type=="dependencies"){for(auto& d:p["dependencies"])if(id.empty()||d["id"]==id||d["instruction"]==id)add(d);}
    else if(type=="data_transfers")collection("dataTransfers");
    else if(type=="instructions"){for(auto it=p["instructions"].begin();it!=p["instructions"].end();++it)if(id.empty()||it.key()==id)add(it.value());}
    else if(type=="transfers"){if(p.contains("transfers"))for(auto& t:p["transfers"])if(id.empty()||t["id"]==id||t["instruction"]==id)add(t);}
    else if(type=="dependency_path"){
        auto depth=bounded(request,"depth",2,16);auto direction=request.value("direction",std::string("suppliers"));if(direction!="suppliers"&&direction!="consumers")throw std::invalid_argument("invalid path direction");
        std::map<std::string,const Json*> ds;for(auto& d:p["dependencies"])ds[d["id"].get<std::string>()]=&d;
        if(!ds.contains(id))throw std::invalid_argument("unknown dependency");
        Graph next;for(auto& e:a["dependencyEdges"]){auto from=e[direction=="suppliers"?"from":"to"].get<std::string>(),to=e[direction=="suppliers"?"to":"from"].get<std::string>();next[from].insert(to);}
        Strings seen{id};std::vector<std::pair<std::string,size_t>> todo{{id,0}};
        for(size_t index=0;index<todo.size();++index){auto [cur,d]=todo[index];own({{"depth",d},{"dependency",*ds[cur]},{"depthLimited",d==depth&&!next[cur].empty()}});
            if(d==depth&&!next[cur].empty())depthLimited=true;
            if(d<depth)for(auto& target:next[cur])if(seen.insert(target).second)todo.emplace_back(target,d+1);
            if(todo.size()>16384){resourceLimited=true;own({{"truncated",true},{"reason","dependency traversal item budget"}});break;}}
    }else throw std::invalid_argument("unknown query type");
    auto limit=bounded(request,"limit",256,1024),offset=bounded(request,"offset",0,1000000);if(limit==0)throw std::invalid_argument("query limit must be positive");
    Json page=Json::array();size_t remaining=Project::defaultBudget;
    for(size_t i=offset;i<items.size()&&page.size()<limit;++i){auto charge=items[i]->dump().size()*2;
        if(charge>remaining){resourceLimited=true;break;}remaining-=charge;page.push_back(*items[i]);}
    if(page.empty()&&offset<items.size())throw std::invalid_argument("query item exceeds response budget; request narrower evidence");
    auto next=offset+page.size();
    return {{"analysisId",a["id"]},{"sourceRevision",a["sourceRevision"]},{"evidenceDigest",a["evidenceDigest"]},{"incomplete",a["incomplete"]},{"truncated",next<items.size()||depthLimited||resourceLimited},{"depthLimited",depthLimited},{"resourceLimited",resourceLimited},
        {"total",decimal(items.size())},{"items",page},{"nextOffset",next<items.size()?Json(next):Json(nullptr)}};
}
void annotateDocument(Json& p,const std::string& instruction,const Json& annotation){
    if(!p.at("instructions").contains(instruction))throw std::invalid_argument("unknown annotation instruction");
    auto kind=annotation.at("kind").get<std::string>(),text=annotation.at("text").get<std::string>();
    if((kind!="inference"&&kind!="confirmed"&&kind!="correction")||text.empty()||text.size()>65536)throw std::invalid_argument("invalid annotation");
    if(annotation.contains("finding")){
        if(!p.contains("analysis")||std::none_of(p["analysis"]["findings"].begin(),p["analysis"]["findings"].end(),[&](auto& f){return f["id"]==annotation["finding"]&&f["instruction"]==instruction;}))throw std::invalid_argument("invalid finding annotation");}
    auto note=annotation;if(note.contains("finding"))note["analysisId"]=p["analysis"]["id"];
    auto& entries=p["annotations"][instruction];if(entries.is_null())entries=Json::array();
    if(std::find(entries.begin(),entries.end(),note)==entries.end()){entries.push_back(note);p["revision"]=decimal(counter(p["revision"])+1);}
}
}
