#include "Session.hpp"
#include <chrono>
#include <set>
namespace BMMQ::Space {
namespace {
uint64_t limit(const Json& command,const char* key,uint64_t fallback,uint64_t maximum){
    auto v=command.value(key,Json(fallback));
    if(!v.is_number_integer()||v<1||v>maximum)throw std::invalid_argument(std::string("invalid exploration limit: ")+key);
    return v.get<uint64_t>();
}
std::set<std::string> evidence(const Json& doc){
    std::set<std::string> result;
    for(auto it=doc["instructions"].begin();it!=doc["instructions"].end();++it)result.insert("instruction:"+it.key());
    for(auto& edge:doc["edges"])result.insert("edge:"+edge.dump());
    if(doc.contains("transfers"))for(auto& transfer:doc["transfers"])if(!transfer["instruction"].get<std::string>().empty())
        result.insert("outcome:"+transfer["instruction"].get<std::string>()+":"+transfer["targetLocation"].get<std::string>()+":"+transfer["taken"].dump());
    for(auto& d:doc["dependencies"])if(d["location"].get<std::string>().starts_with("m:")&&counter(Json(d["location"].get<std::string>().substr(2)))>>32>=4)
        result.insert("hardware:"+d["instruction"].get<std::string>()+":"+d["location"].get<std::string>()+":"+d["access"].get<std::string>()+":"+d["value"].dump());
    return result;
}
Json unresolved(const Json& doc){
    Json result=Json::array();std::set<std::string> locations;
    for(auto it=doc["instructions"].begin();it!=doc["instructions"].end();++it)locations.insert(it.value()["location"].get<std::string>());
    std::set<std::string> seen;
    for(auto& edge:doc["edges"])if(!locations.contains(edge["targetLocation"].get<std::string>())){
        auto key=edge["from"].get<std::string>()+":"+edge["targetLocation"].get<std::string>();
        if(seen.insert(key).second)result.push_back({{"instruction",edge["from"]},{"targetLocation",edge["targetLocation"]},{"kind",edge["kind"]}});
    }
    return result;
}
}
Json Session::autoExplore(const Json& command){
    // The control plane owns checkpoints and interpreter control; the worker only
    // consumes capture packets. No branch writes CPU state or fabricates coverage.
    auto maxSteps=limit(command,"maxSteps",10000000,100000000);
    auto maxAttempts=limit(command,"maxAttempts",128,4096);
    auto maxStagnant=limit(command,"stagnantAttempts",16,4096);
    auto perAttempt=limit(command,"stepsPerAttempt",10000,1000000);
    auto masks=command.value("masks",Json::array({0,1,2,4,8,16,32,64,128,3,12,48,192,17,34,255}));
    if(!masks.is_array()||masks.empty()||masks.size()>256)throw std::invalid_argument("invalid exploration input schedule");
    for(auto& mask:masks)if(!mask.is_number_integer()||mask<0||mask>255)throw std::invalid_argument("invalid exploration input mask");
    auto scenarios=command.value("scenarios",Json::array());
    if(!scenarios.is_array()||scenarios.size()>128)throw std::invalid_argument("invalid exploration scenarios");
    std::set<std::string> ids;
    for(auto& scenario:scenarios){
        auto id=scenario.at("id").get<std::string>();
        if(id.empty()||id.size()>256||!ids.insert(id).second)throw std::invalid_argument("invalid scenario id");
        auto& actions=scenario.at("actions");
        if(!actions.is_array()||actions.empty()||actions.size()>256)throw std::invalid_argument("invalid scenario actions");
        for(auto& action:actions){auto mask=action.at("mask");if(!mask.is_number_integer()||mask<0||mask>255)throw std::invalid_argument("invalid scenario input");(void)limit(action,"steps",1,1000000);}
    }
    auto checkpoints=command.value("checkpoints",Json::array());
    if(!checkpoints.is_array()||checkpoints.size()>128)throw std::invalid_argument("invalid exploration checkpoints");
    for(auto& path:checkpoints)if(!path.is_string()||!std::filesystem::is_directory(path.get<std::string>()))throw std::invalid_argument("missing exploration checkpoint");
    const auto startingFingerprint=fingerprint();auto initial=document();auto known=evidence(initial);
    if(capture_->stopped()||!initial["gaps"].empty())
        throw ExecutionPaused("automatic exploration requires complete ongoing capture");
    auto temporary=std::filesystem::temp_directory_path()/("time-space-exploration-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(temporary))throw std::runtime_error("cannot create exploration workspace");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{temporary};
    checkpoint(temporary/"root");checkpoints.insert(checkpoints.begin(),(temporary/"root").string());
    Json attempts=Json::array(),completed=Json::array();uint64_t steps=0,stagnant=0;std::string reason="attempt_limit";
    for(uint64_t attempt=0;attempt<maxAttempts;++attempt){
        if(steps>=maxSteps){reason="step_limit";break;}
        if(stagnant>=maxStagnant){reason="stagnation";break;}
        auto checkpointIndex=attempt%checkpoints.size();
        restore(checkpoints[checkpointIndex].get<std::string>());
        auto branchStart=document();auto goals=unresolved(branchStart);
        Json actions=Json::array();std::string scenarioId;
        if(attempt<scenarios.size()){scenarioId=scenarios[attempt]["id"].get<std::string>();actions=scenarios[attempt]["actions"];}
        else actions.push_back({{"mask",masks[(attempt-scenarios.size())%masks.size()]},{"steps",perAttempt}});
        uint64_t executed=0,navigationSteps=0;std::string termination="schedule_complete";
        Json selectedTarget;
        if(scenarioId.empty()&&!goals.empty()){
            selectedTarget=goals[(attempt-scenarios.size())%goals.size()];
            const auto& source=branchStart["instructions"][selectedTarget["instruction"].get<std::string>()];
            try{while(navigationSteps<perAttempt/2&&steps<maxSteps){
                auto pc=core_->memory().file.findRegister("PC")->reg->value;
                if(pc==source["address"]&&decimal(core_->location(pc))==source["location"].get<std::string>()&&!core_->boundaryOnly())break;
                step();++navigationSteps;++steps;++executed;
                if(capture_->stopped()||executionStatus()["activeMode"]=="paused"){termination="evidence_loss";break;}
            }}catch(const ExecutionPaused&){termination="execution_pause";}
        }
        Json applied=Json::array();
        for(auto& action:actions){
            if(termination!="schedule_complete"||steps>=maxSteps)break;
            input(action["mask"].get<uint8_t>());uint64_t count=0,bound=std::min(limit(action,"steps",1,1000000),maxSteps-steps);
            try{for(;count<bound;++count){step();++steps;++executed;if(capture_->stopped()||executionStatus()["activeMode"]=="paused"){++count;termination="evidence_loss";break;}}}
            catch(const ExecutionPaused&){termination="execution_pause";}
            applied.push_back({{"mask",action["mask"]},{"steps",decimal(count)}});
            if(count<limit(action,"steps",1,1000000)&&termination=="schedule_complete")termination="step_limit";
            if(termination!="schedule_complete"||steps>=maxSteps)break;
        }
        auto current=document();auto observed=evidence(current);size_t added=0;
        for(auto& key:observed)if(known.insert(key).second)++added;
        stagnant=added?0:stagnant+1;
        if(!scenarioId.empty()&&termination=="schedule_complete"&&applied.size()==actions.size())completed.push_back(scenarioId);
        attempts.push_back({{"attempt",attempt},{"checkpointIndex",checkpointIndex},{"scenario",scenarioId},{"priority",scenarioId.empty()?"unresolved_transfers":"missing_scenario"},
            {"targets",goals},{"selectedTarget",selectedTarget},{"navigationSteps",decimal(navigationSteps)},{"actions",applied},{"steps",decimal(executed)},{"newEvidence",added},{"termination",termination},{"fingerprint",fingerprint()}});
        if(!current["gaps"].empty()||termination=="execution_pause"||termination=="evidence_loss"){reason=termination=="schedule_complete"?"evidence_loss":termination;break;}
    }
    if(steps>=maxSteps)reason="step_limit";else if(stagnant>=maxStagnant)reason="stagnation";
    Json missing=Json::array();for(auto& scenario:scenarios)if(std::find(completed.begin(),completed.end(),scenario["id"])==completed.end())missing.push_back(scenario["id"]);
    return {{"schemaVersion",1},{"core",initial["core"]},{"romSha256",initial["romSha256"]},{"startingFingerprint",startingFingerprint},
        {"limits",{{"maxSteps",maxSteps},{"maxAttempts",maxAttempts},{"stagnantAttempts",maxStagnant},{"stepsPerAttempt",perAttempt}}},
        {"reason",reason},{"steps",decimal(steps)},{"attempts",attempts},{"stuck",{{"unresolvedTransfers",unresolved(document())},{"missingScenarios",missing},{"consecutiveAttemptsWithoutEvidence",stagnant}}},
        {"limitations",Json::array({"Input schedules explore checkpoint branches; absence of discovery does not prove unreachable code.","Completed scenarios mean schedules executed, not independent verification of gameplay obligations."})}};
}
}
